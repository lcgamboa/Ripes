#pragma once

#include "../binutils.h"
#include "assembler_defines.h"

#include <QString>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>
#include "math.h"

namespace Ripes {

namespace AssemblerTmp {

namespace {
using AssembleRes = std::variant<AssemblerTmp::Error, uint32_t>;
using PseudoExpandRes = std::variant<AssemblerTmp::Error, std::vector<AssemblerTmp::LineTokens>>;
using DisassembleRes = std::variant<AssemblerTmp::Error, AssemblerTmp::LineTokens>;
}  // namespace

struct BitRange {
    constexpr BitRange(unsigned _start, unsigned _stop, unsigned _N = 32) : start(_start), stop(_stop), N(_N) {
        assert(isPowerOf2(_N) && "Bitrange N must be power of 2");
        assert(0 <= _start && _start <= _stop && _stop < _N && "invalid range");
    }
    constexpr unsigned width() const { return stop - start + 1; }
    const unsigned start, stop, N;
    const uint32_t mask = generateBitmask(width());

    uint32_t apply(uint32_t value) const { return (value & mask) << start; }
    uint32_t decode(uint32_t instruction) const { return (instruction >> start) & mask; };

    bool operator==(const BitRange& other) const { return this->start == other.start && this->stop == other.stop; }
    bool operator<(const BitRange& other) const { return this->start < other.start; }
};

struct Field {
    Field() = default;
    virtual ~Field() = default;
    virtual std::optional<AssemblerTmp::Error> apply(const AssemblerTmp::SourceLine& line,
                                                     uint32_t& instruction) const = 0;
    virtual std::optional<AssemblerTmp::Error> decode(const uint32_t instruction, const uint32_t address,
                                                      const ReverseSymbolMap* symbolMap,
                                                      AssemblerTmp::LineTokens& line) const = 0;
};

/** @brief OpPart
 * A segment of an operation-identifying field of an instruction.
 */
struct OpPart {
    OpPart(unsigned _value, BitRange _range) : value(_value), range(_range) {}
    OpPart(unsigned _value, unsigned _start, unsigned _stop) : value(_value), range({_start, _stop}) {}
    constexpr bool matches(uint32_t instruction) const { return range.decode(instruction) == value; }
    const unsigned value;
    const BitRange range;

    bool operator==(const OpPart& other) const { return this->value == other.value && this->range == other.range; }
    bool operator<(const OpPart& other) const { return this->range < other.range || this->value < other.value; }
};

struct Opcode : public Field {
    /**
     * @brief Opcode
     * @param name: Name of operation
     * @param fields: list of OpParts corresponding to the identifying elements of the opcode.
     */
    Opcode(const QString& _name, std::vector<OpPart> _opParts) : name(_name), opParts(_opParts) {}

    std::optional<AssemblerTmp::Error> apply(const AssemblerTmp::SourceLine&, uint32_t& instruction) const override {
        for (const auto& opPart : opParts) {
            instruction |= opPart.range.apply(opPart.value);
        }
    }
    std::optional<AssemblerTmp::Error> decode(const uint32_t, const uint32_t, const ReverseSymbolMap*,
                                              AssemblerTmp::LineTokens& line) const override {
        line.push_back(name);
        return {};
    }

    bool operator==(uint32_t instruction) const {}

    const QString name;
    const std::vector<OpPart> opParts;
};

template <typename ISA>
struct Reg : public Field {
    /**
     * @brief Reg
     * @param tokenIndex: Index within a list of decoded instruction tokens that corresponds to the register index
     * @param range: range in instruction field containing register index value
     */
    Reg(unsigned tokenIndex, BitRange range) : m_tokenIndex(tokenIndex), m_range(range) {}
    Reg(unsigned tokenIndex, unsigned _start, unsigned _stop) : m_tokenIndex(tokenIndex), m_range({_start, _stop}) {}
    std::optional<AssemblerTmp::Error> apply(const AssemblerTmp::SourceLine& line,
                                             uint32_t& instruction) const override {
        bool success;
        const QString& regToken = line.tokens[m_tokenIndex];
        const uint32_t reg = ISA::instance()->regNumber(regToken, success);
        if (!success) {
            return AssemblerTmp::Error(line.source_line, "Unknown register '" + regToken + "'");
        }
        instruction |= m_range.apply(reg);
    }
    std::optional<AssemblerTmp::Error> decode(const uint32_t instruction, const uint32_t address,
                                              const ReverseSymbolMap* symbolMap,
                                              AssemblerTmp::LineTokens& line) const override {
        const unsigned regNumber = m_range.decode(instruction);
        const QString registerName = ISA::instance()->regName(regNumber);
        if (registerName.isEmpty()) {
            return AssemblerTmp::Error(0, "Unknown register number '" + QString::number(regNumber) + "'");
        }
        line.append(registerName);
        return {};
    }

    const unsigned m_tokenIndex;
    const BitRange m_range;
};

struct ImmPart {
    ImmPart(unsigned _offset, BitRange _range) : offset(_offset), range(_range) {}
    ImmPart(unsigned _offset, unsigned _start, unsigned _stop) : offset(_offset), range({_start, _stop}) {}
    void apply(const uint32_t& value, uint32_t& instruction) const { instruction |= range.apply(value >> offset); }
    void decode(uint32_t& value, const uint32_t& instruction) const { value |= range.decode(instruction) << offset; };
    const unsigned offset;
    const BitRange range;
};

struct Imm : public Field {
    enum class Repr { Unsigned, Signed, Hex };
    enum class SymbolType { None, Relative, Absolute };
    /**
     * @brief Imm
     * @param tokenIndex: Index within a list of decoded instruction tokens that corresponds to the immediate
     * @param ranges: (ordered) list of ranges corresponding to fields of the immediate
     * @param symbolType: Set if this immediate refers to a relative or absolute symbol.
     */
    Imm(unsigned _tokenIndex, unsigned _width, Repr _repr, const std::vector<ImmPart>& _parts,
        SymbolType _symbolType = SymbolType::None)
        : tokenIndex(_tokenIndex), parts(_parts), width(_width), repr(_repr), symbolType(_symbolType) {}

    std::optional<AssemblerTmp::Error> apply(const AssemblerTmp::SourceLine& line,
                                             uint32_t& instruction) const override {
        // @Todo: decode immediate from token (appropriate bit width!), apply each ImmPart with immediate to instruction
    }
    std::optional<AssemblerTmp::Error> decode(const uint32_t instruction, const uint32_t address,
                                              const ReverseSymbolMap* symbolMap,
                                              AssemblerTmp::LineTokens& line) const override {
        uint32_t reconstructed = 0;
        for (const auto& part : parts) {
            part.decode(reconstructed, instruction);
        }
        if (repr == Repr::Signed) {
            line.push_back(QString::number(static_cast<int32_t>(signextend<int32_t>(reconstructed, width))));
        } else if (repr == Repr::Unsigned) {
            line.push_back(QString::number(reconstructed));
        } else {
            line.push_back("0x" + QString::number(reconstructed, 16));
        }

        if (symbolType != SymbolType::None && symbolMap != nullptr) {
            const int value = signextend<int32_t>(reconstructed, width);
            const uint32_t symbolAddress = value + (symbolType == SymbolType::Absolute ? 0 : address);
            if (symbolMap->count(symbolAddress)) {
                line.push_back("<" + symbolMap->at(symbolAddress) + ">");
            }
        }

        return {};
    }

    const unsigned tokenIndex;
    const std::vector<ImmPart> parts;
    const unsigned width;
    const Repr repr;
    const SymbolType symbolType;
};

template <typename ISA>
class Instruction {
public:
    Instruction(Opcode opcode, const std::vector<std::shared_ptr<Field>>& fields)
        : m_opcode(opcode), m_expectedTokens(1 /*opcode*/ + fields.size()), m_fields(fields) {
        m_assembler = [this](const AssemblerTmp::SourceLine& line) {
            uint32_t instruction = 0;
            m_opcode.apply(line, instruction);
            for (const auto& field : m_fields)
                field->apply(line, instruction);
            return instruction;
        };
        m_disassembler = [this](const uint32_t instruction, const uint32_t address, const ReverseSymbolMap* symbolMap) {
            AssemblerTmp::LineTokens line;
            m_opcode.decode(instruction, address, symbolMap, line);
            for (const auto& field : m_fields) {
                if (auto error = field->decode(instruction, address, symbolMap, line)) {
                    return DisassembleRes(*error);
                }
            }
            return DisassembleRes(line);
        };
    }

    AssembleRes assemble(const AssemblerTmp::SourceLine& line) {
        if (line.tokens.length() != m_expectedTokens) {
            return AssemblerTmp::Error(line.source_line, "Instruction '" + m_opcode.name + "' expects " +
                                                             QString::number(m_expectedTokens - 1) +
                                                             " tokens, but got " +
                                                             QString::number(line.tokens.length() - 1));
        }

        return m_assembler(line);
    }
    DisassembleRes disassemble(const uint32_t instruction, const uint32_t address,
                               const ReverseSymbolMap* symbolMap) const {
        return m_disassembler(instruction, address, symbolMap);
    }

    const QString& name() const { return m_opcode; }

private:
    std::function<AssembleRes(const AssemblerTmp::SourceLine&)> m_assembler;
    std::function<DisassembleRes(const uint32_t, const uint32_t, const ReverseSymbolMap*)> m_disassembler;

    const Opcode m_opcode;
    const int m_expectedTokens;
    const std::vector<std::shared_ptr<Field>> m_fields;
};

template <typename ISA>
class PseudoInstruction {
public:
    PseudoInstruction(
        const QString& opcode, const std::vector<std::shared_ptr<Field>>& fields,
        const std::function<PseudoExpandRes(const PseudoInstruction&, const AssemblerTmp::SourceLine&)>& expander)
        : m_opcode(opcode), m_expectedTokens(1 /*opcode*/ + fields.size()), m_fields(fields), m_expander(expander) {}

    PseudoExpandRes expand(const AssemblerTmp::SourceLine& line) {
        if (line.tokens.length() != m_expectedTokens) {
            return AssemblerTmp::Error(
                line.source_line, "Instruction '" + m_opcode + "' expects " + QString::number(m_expectedTokens - 1) +
                                      " tokens, but got " + QString::number(line.tokens.length() - 1));
        }

        return m_expander(line);
    }

    const QString& name() const { return m_opcode; }

private:
    std::function<PseudoExpandRes(const PseudoInstruction& /*this*/, const AssemblerTmp::SourceLine&)> m_expander;

    const QString m_opcode;
    const int m_expectedTokens;
    const std::vector<std::shared_ptr<Field>> m_fields;
};

}  // namespace AssemblerTmp

}  // namespace Ripes
