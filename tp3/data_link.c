#include "data_link.h"


int ns=0, nr=0;

int resend=0, conta=0;

char global_flag;

struct termios oldtio,newtio;

void atende()                   // atende alarme
{
	printf("alarme # %d\n", conta);
	resend=1;
}

int llopen(char * porta, char flag){
    
    int fd,c, res;
    
    char buf[255]="";
    int i, sum = 0, speed = 0;
   
    global_flag = flag;

    fd = open(porta, O_RDWR | O_NOCTTY );
    if (fd <0) {perror(porta); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 5;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received */

    (void) signal(SIGALRM, atende); 


    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

   
    if(flag==TRANSMITTER){

          StateMachine1 machine;
          
          assembleStateMachine(&machine,A_RSP_RECETOR,UA);

          do {
            conta++;
            i=0;
            resend = 0;

            printf("Before send set\n");
            send_set(fd);

            alarm(3);

            while (machine.state != STOP && !resend) {       /* loop for input */
              
              res = read(fd,&buf[i],1);   /* returns after 1 char has been input */
              if (res == 0) continue;
              printf("byte: %x\n", buf[i]);
              machine.byte = buf[i];
              changeState(&machine);
              i++;
            }

          } while (conta< 3 && machine.state != STOP);

          
          if(conta>=3){
            printf("UA not received, exiting\n");
            sleep(1);
            if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
              perror("tcsetattr");
              exit(-1);
            }
            close(fd);
            return -1;
          }
          else {printf("UA received \n");
          alarm(0);
          }
    }

    else if(flag==RECEIVER){
          newtio.c_lflag = 0;
          newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
          newtio.c_cc[VMIN]     = 1; 

          tcflush(fd, TCIOFLUSH);

          if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
            perror("tcsetattr");
            exit(-1);
          }
        

          StateMachine1 machine_r;
          assembleStateMachine(&machine_r,A_CMD_EMISSOR,SET);

          char buf[255]="";
        
          int stop = 0;
          int i = 0;
          while(!stop)
          {
            res = read(fd, &buf[i], 1);
            printf("byte read: %x\n", buf[i]);
            machine_r.byte = buf[i];
            changeState(&machine_r);
            if(machine_r.state==STOP)stop = 1;
            i++;
          }
          printf("SET received! \n");

          send_ua(fd, RECEIVER);
  }
    
    else  return -1;

    
    return fd;

}

int llwrite(int fd, char * buffer, int length){
    resend=0;
    conta=0;
    int sentSucess=FALSE;
    unsigned char buf[255]="";
    int res, i=0;
    char frame[length + 6];

    StateMachine2 machine;
    assembleStateMachine2(&machine,A_CMD_EMISSOR, !ns);
    createInfoFrame(buffer,ns, frame);
    do {
      conta++;
      i=0;
      resend = 0;
    
      write(fd, frame,length+6);
      printf("Info sent\n");
      alarm(3);
      
      while (machine.state != STOP && !resend) {       /* loop for input */
        
        res = read(fd,&buf[i],1);   /* returns after 1 char has been input */
        if (res == 0) continue;
        printf("byte: %x\n", buf[i]);
        machine.byte = buf[i];
        changeState2(&machine);
        if(machine.state==C_RCV){
            if(ns!=0){
              if(machine.byte==RR)sentSucess=TRUE;
              else resend=TRUE;
            }
            else{
              if(machine.byte==RR | 0x80)sentSucess=TRUE;
              else resend=TRUE;
            }
        }
        i++;
    }

  } while (!sentSucess && resend && conta < 3);

  
  if(conta>=3){
    printf("Exceded number of tries to send frame \n");
    return -1;
  }
  else{
    alarm(0);
    sentSucess=FALSE;
  }

  if(ns==0)ns=1;
  else ns=0;
  return length;
}




int llread(int fd, char * buffer){

  int descartarTrama = FALSE;


  StateMachineInfo machine;
  assembleStateMachineInfo(&machine,A_CMD_EMISSOR, !nr);

  unsigned char byte;
  unsigned char info[255] = "";
  int i = 0, infoSize=0;
  while(machine.state!=STOP)
  {
    read(fd, &byte, 1);
    printf("byte read: %x\n", byte);
    machine.byte = byte;
    changeInfoState(&machine);
    i++;

     if (machine.state == DATA_OK && byte != FLAG)
    {
      info[infoSize] = byte;
      infoSize++;
    }

     if (i== 3)
    {
     
      if (nr == 1 && byte == C_NS1)
        descartarTrama = TRUE;
      
      else if (nr == 0 && byte == C_NS0)
        descartarTrama = TRUE;
      
        
    }
}


  byte_destuffing(info, buffer);
  int dataError= !check_destuffing(info,info[strlen(info)-1]);

  if(!descartarTrama){
    if(dataError){
      send_rej(fd,nr);
      
      return -1;
    }
    else {
      send_rr(fd,nr);    
      if(nr==0)nr=1;
      else nr=0;
  
      return strlen(buffer);
    }
  }
  else{
  
    send_rr(fd,nr);
    return -1;
  }






  
}

int llclose(int fd){
    
  if(global_flag == TRANSMITTER){

    StateMachine1 disc_machine;
    assembleStateMachine(&disc_machine, A_CMD_RECETOR, DISC);
    int i, res;
    char buf[255]="";

    do {
            conta++;
            i=0;
            resend = 0;

            printf("Before send disc\n");
            send_disc(fd, TRANSMITTER);

            alarm(3);

            while (disc_machine.state != STOP && !resend) {       /* loop for input */
              
              res = read(fd,&buf[i],1);   /* returns after 1 char has been input */
              if (res == 0) continue;
              printf("byte: %x\n", buf[i]);
              disc_machine.byte = buf[i];
              changeState(&disc_machine);
              i++;
            }

          } while (conta< 3 && disc_machine.state != STOP);

          
          if(conta>=3){
            printf("DISC not received, exiting\n");
            sleep(1);
            close(fd);
            return -1;
          }
          else printf("DISC received\n");

          send_ua(fd, TRANSMITTER);
  }
  else if(global_flag = RECEIVER){

    StateMachine1 machine_r;
    assembleStateMachine(&machine_r,A_CMD_EMISSOR,DISC);

    char buf[255]="";
    int res;
  
    int stop = 0;
    int i = 0;
    while(!stop)
    {
      res = read(fd, &buf[i], 1);
      printf("byte read: %x\n", buf[i]);
      machine_r.byte = buf[i];
      changeState(&machine_r);
      if(machine_r.state==STOP)stop = 1;
      i++;
    }
    printf("DISC received! \n");

    send_disc(fd, RECEIVER);


  }
  else return -1;

  sleep(1);
  tcsetattr(fd,TCSANOW,&oldtio);
  close(fd);

  return 0;
}