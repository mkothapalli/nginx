/*******************************************
 * Date: 07/09/2015
 * Functionality: This program generates random urls that can be used
 *                to test the trackuri http module.
 *                The program assumes that we are testing an nginx instance
 *                running on localhost port 8080. Modify the program if that
 *                is not the case.
 *   
 * Usage: 
 * $ make
 * $ ./generateurls > urls
 * $ chmod +x urls
 * $ ./urls
 ********************************************/
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#define MAX_URLS 10000
#define UNIQUE_URLS 1000
#define LOCATION "/images/"
#define NGINX_HOST "localhost"
#define NGINX_PORT "8080"
 
int main()
{
  srand(time(NULL));
  for (int i = 0; i < MAX_URLS; i++)
  {
    int r = rand() % UNIQUE_URLS;
    printf("curl -I http://%s:%s%s%d.html\n", NGINX_HOST, NGINX_PORT, LOCATION, r);
  }

  // last request must be a GET request that generates the top-n report in the response.
  int r = rand() % UNIQUE_URLS;
  printf("curl http://%s:%s%s%d.html\n", NGINX_HOST, NGINX_PORT, LOCATION, r);
  return 0;
}
