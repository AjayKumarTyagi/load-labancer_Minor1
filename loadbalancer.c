#include <curl/curl.h>  
#include <stdio.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<string.h> // for strlen function
#include<unistd.h>	//write
#include <sys/ipc.h> 
#include <sys/shm.h>   // shared memeory
#include<pthread.h> //for threading , link with lpthread

void connect_backend_server(int); 
 char* roundRobin();
void *connection_handler(void *);


// inital current server value
int current_server=-1;

// total number of server
int total_server=2;

// back end servers
const char a[2][20]={
 "localhost:8888",
 "localhost:8000",

};

 int *hc;   //  pointer for array which contain current health of the back end server

// character arry for response heander
char res_header[100]; 

struct MemoryStruct {
  char *memory;
	size_t size;
};


static size_t header_callback(char *buffer, size_t size,
                              size_t nitems, void *userdata)
{
      
  size_t numbytes = size * nitems;
    printf("%.*s\n", numbytes, buffer);

  // storing response header
   strcat(res_header,buffer);
 
    return numbytes;
}

void *healthCheck(void *vargp){
printf("\nhealth check\n");
CURL *curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();

int id=(int)vargp;

char rq[]="http://",endpoint[]="/healthcheck";
char host[30];
strcpy(host,a[id]);
strcat(host,endpoint);
	
if(curl) {
    
curl_easy_setopt(curl, CURLOPT_URL, strcat(rq,host));
    
curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
/* Check for errors */
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
       hc[id]=0;     // if no response from the server
       else{
        hc[id]=1;    // get response from the server
       }
   
    curl_easy_cleanup(curl);
    
  /* always cleanup */
  }
 
  curl_global_cleanup();
}

// round robin implentation
char* roundRobin(){


if(current_server==1)
 current_server=-1;


  int i=0;

for(i=current_server+1;i<total_server;i++){
// checking if the current server is healthy or not
  if(hc[i]!=0){
    current_server=i;
    break;
}
}

 


char rq[]="http://";
char host[30];

strcpy(host,a[current_server]);

return strcat(rq,host);

}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb,
		void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *) userp;

	mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		/* out of memory! */
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}


// back end server connection
void connect_backend_server(int client_sd){


  char h[65535];
 


    memset(h, '\0', sizeof(h));
      read(client_sd , h ,sizeof(h));
        
        fputs(h,stdout);

CURL *curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();
  if(curl) {

	   struct MemoryStruct s;
          struct curl_slist *chunk = NULL;

// extrating headers
  

    char *token = strtok(h, "\r\n"); 
     char endpoint[30];
     int k=0;
     while (token != NULL) 
    { 
      printf("%s\n", token); 
      chunk =curl_slist_append(chunk , token);
      if(k==0)
       strcpy(endpoint,token);
      token = strtok(NULL, "\r\n"); 
      k++;
    } 
   
    // extracting endpoint
    k=0;
  token=strtok(endpoint," ");
  while (token != NULL) 
    { 
      printf("%s\n", token); 
      chunk =curl_slist_append(chunk , token);
      if(k==1)
       strcpy(endpoint,token);
      token = strtok(NULL, " "); 
      k++;
    } 



    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, strcat(roundRobin(),endpoint));
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    
    /* Perform the request, res will get the return code */
    res = curl_easy_perform(curl);
    /* Check for errors */
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
	  
    
   // write response headers 
    write(client_sd,res_header,strlen(res_header));
   memset(res_header, '\0', sizeof(res_header));

    write(client_sd , s.memory ,s.size);
    free(s.memory);
	  
    /* always cleanup */
    curl_easy_cleanup(curl);

   curl_slist_free_all(chunk);
  }

  curl_global_cleanup();
 
  // closing client socket
   shutdown(client_sd, SHUT_WR);
   close(client_sd);


}

int main(int argc , char *argv[]){
	
	key_t key = ftok("shmfile",65); 
  // shared memeory
  // ftok to generate unique key 
    int shmid = shmget(IPC_PRIVATE,2*1024,0666|IPC_CREAT);
	// shmget returns an identifier in shmid
    hc = (int*) shmat(shmid,NULL,0); // shmat to attach to shared memory

//creating a socket
int socket_descreptor = socket(AF_INET, SOCK_STREAM, 0);

if( socket_descreptor == -1)
  printf("Could not able to create a socket");


//prepare the socket address in structor

struct sockaddr_in server;

server.sin_family= AF_INET;
server.sin_addr.s_addr= INADDR_ANY;
server.sin_port = htons(5000);


// binding the socket with the port
 if(bind(socket_descreptor,(struct sockaddr *)&server ,sizeof(server))< 0){

         puts("socket bind failed");
         return 1;
  }


  
// listen for the incoming port

listen(socket_descreptor, 5);
puts("listining for the incoming requests");
 

//accepting incoming requests

int c = sizeof( struct sockaddr_in);
int new_socket;
struct sockaddr_in client;
int fork_value;
	
// fork to run two process parallel
fork_value= fork();

//child
if(fork_value==0){
    pthread_t tid; 

int i;

// calling healthcheck after every 5 sec 
  while(1){
    for(i=0;i<total_server;i++)
      pthread_create(&tid, NULL, healthCheck,(void *)i); 
 sleep(5);
}

}

if(fork_value!=0){
// accepting incoming client request
while((new_socket = accept(socket_descreptor ,(struct sockaddr *)&client, (socklen_t *)&c))){
  
      puts("request accepted");

     

       pthread_t sniffer_thread;

       new_sock = malloc(1);
       *new_sock = new_socket;

      if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*)new_sock) < 0)
		{
			perror("could not create thread");
			return 1;
		}
		

}

 if(new_socket<0)
  {
      puts("request accept failed");
      return 0;
}

}
	
shmctl(shmid, IPC_RMID, NULL);
return 0;

}

void *connection_handler(void *socket_desc){

	int sock = *(int*)socket_desc;

         connect_backend_server(sock);

}

