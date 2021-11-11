#include"tcp.h"
#include "vbus.h"

#define dprintf if (1) {} else printf


int32_t recv_cmd (cmd_header_t * cmd_header);
int32_t recv_payload (char * buff, const uint32_t payload_size);
int32_t send_cmd (const uint32_t cmd, const char * payload = NULL, const uint32_t payload_size = 0);


int sockfd = -1;

const char json_info[] = "{"
        "  \"name\": \"PICSimLab\","
        "  \"description\": \"PICSimLab IO\","
        "  \"base address\": 0,"
        "  \"address width\": 4,"
        "  \"symbols\": {"
        "    \"PORTA\": 0,"
        "    \"DIRA\": 4,"
        "    \"PORTB\": 8,"
        "    \"DIRB\": 12"
        "  }"
        "}";

uint32_t regs[8];

int
main (int argc, char *argv[]) /* simple TCP server */
{
  struct sockaddr_in serv, cli;
  int listenfd;
#ifdef WIN
  int clilen;
#else
  unsigned int clilen;
#endif

#ifdef WIN
  WSAStartup (wVersionRequested, &wsaData);
  if (wsaData.wVersion != wVersionRequested)
    {
      fprintf (stderr, "\n Wrong version\n");
      exit (-1);
    }
#endif

  regs[0] = 0;
  regs[1] = 0;
  regs[2] = 0;
  regs[3] = 0;
  regs[4] = 0;
  regs[5] = 0;
  regs[6] = 0;
  regs[7] = 0;



  if ((listenfd = socket (PF_INET, SOCK_STREAM, 0)) < 0)
    {
      printf ("socket error : %s \n", strerror (errno));
      exit (1);
    };
  int reuse = 1;
  if (setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuse, sizeof (reuse)) < 0)
    perror ("setsockopt(SO_REUSEADDR) failed");

  memset (&serv, 0, sizeof (serv));
  serv.sin_family = AF_INET;
  serv.sin_addr.s_addr = htonl (INADDR_ANY);
  serv.sin_port = htons (7890);

  if (bind (listenfd, (sockaddr *) & serv, sizeof (serv)) < 0)
    {
      printf ("bind error : %s \n", strerror (errno));
      exit (1);
    };

  if (listen (listenfd, SOMAXCONN) < 0)
    {
      printf ("listen error : %s \n", strerror (errno));
      exit (1);
    };
    
    printf("Server started\n");

  for (;;)
    {
      clilen = sizeof (cli);
      if (
          (sockfd =
           accept (listenfd, (sockaddr *) & cli, & clilen)) < 0)
        {
          printf ("accept error : %s \n", strerror (errno));
          exit (1);
        }
      
      printf("Client connected\n");

      int exit_loop = 0;
      while (!exit_loop)
        {
          cmd_header_t cmd_header;

          if (recv_cmd (&cmd_header) < 0)
            {
              exit_loop = 1;
              break;
            }

          dprintf ("MSG type = %i size=%i ", cmd_header.msg_type, cmd_header.payload_size);

          switch (cmd_header.msg_type)
            {
            case VB_PINFO:
              if (send_cmd (VB_PINFO, json_info, strlen (json_info)) < 0)
                {
                  exit_loop = 1;
                  break;
                }

              dprintf ("VB_PINFO %s\n", json_info);
              break;
            case VB_PWRITE:
              {
                if (cmd_header.payload_size)
                  {
                    uint32_t * payload = new uint32_t[cmd_header.payload_size / 4];
                    if (recv_payload ((char *) payload, cmd_header.payload_size) < 0)
                      {
                        exit_loop = 1;
                        break;
                      }
                    for (uint32_t i = 0; i < (cmd_header.payload_size / 4); i++)
                      {
                        payload[i] = ntohl (payload[i]);
                      }
                    if (payload[0]/4 < 8)
                      {
                        regs[payload[0]/4] = payload[1];
                        printf ("VB_PWRITE reg[%i] = %x\n", payload[0], payload[1]);
                      }
                    delete[] payload;
                  }
                if (send_cmd (cmd_header.msg_type) < 0)
                  {
                    exit_loop = 1;
                    break;
                  }
              }
              break;
            case VB_PREAD:
              {
                uint32_t addr = 0;
                if (cmd_header.payload_size)
                  {
                    recv_payload ((char *) &addr, 4);
                    addr = ntohl (addr);
                  }

                if (addr/4 > 8)
                  {
                    printf ("Read invalid reg addr %i !!!!!!!!!!!!!!!!!!\n", addr);
                    addr = 0;
                  }
                uint32_t payload[2];
                payload[0] = htonl (addr);
                payload[1] = htonl (regs[addr/4]);
                if (send_cmd (cmd_header.msg_type, (const char *) &payload, 8) < 0)
                  {
                    exit_loop = 1;
                    break;
                  }
                dprintf ("VB_PREAD  reg[%x] = %x \n", addr, regs[addr]);
              }
              break;
            case VB_DMARD:
            case VB_DMAWR:
            case VB_PSTATUS:
              //break;
            default:
              printf ("Invalid cmd !!!!!!!!!!!!\n");
              if (send_cmd (VB_LAST))
                {
                  exit_loop = 1;
                  break;
                }
              break;
            }
        }

      close (sockfd);
    };
#ifdef WIN
  WSACleanup ();
#endif
}

int32_t
recv_payload (char * buff, const uint32_t payload_size)
{
  char * dp = buff;
  int ret = 0;
  uint32_t size = payload_size;
  do
    {
      if ((ret = recv (sockfd, dp, size, MSG_WAITALL)) != (int) size)
        {
          printf ("receive error : %s \n", strerror (errno));
          return -1;
        }
      size -= ret;
      dp += ret;
    }
  while (size);

  return ret;
}

int32_t
send_cmd (const uint32_t cmd, const char * payload, const uint32_t payload_size)
{
  uint32_t ret;
  cmd_header_t cmd_header;

  cmd_header.msg_type = htonl (cmd);
  cmd_header.payload_size = htonl (payload_size);
  if ((ret = send (sockfd,(const char *) &cmd_header, sizeof ( cmd_header), MSG_NOSIGNAL)) != sizeof ( cmd_header))
    {
      printf ("send error : %s \n", strerror (errno));
      return -1;
    }
  if (payload_size)
    {
      if ((ret += send (sockfd, payload, payload_size, MSG_NOSIGNAL)) != payload_size + sizeof ( cmd_header))
        {
          printf ("send error : %s \n", strerror (errno));
          return -1;
        }
    }
  return ret;
}

int32_t
recv_cmd (cmd_header_t * cmd_header)
{

  char * dp = (char *) (cmd_header);
  int ret = 0;
  int size = sizeof ( cmd_header);
  do
    {
      if ((ret = recv (sockfd, dp, size, MSG_WAITALL)) != size)
        {
          printf ("receive error : %s \n", strerror (errno));
          return -1;
        }

      size -= ret;
      dp += ret;
    }
  while (size);

  cmd_header->msg_type = ntohl (cmd_header->msg_type);
  cmd_header->payload_size = ntohl (cmd_header->payload_size);

  return ret;
}
