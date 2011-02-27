/**
 * Make sure serial monitor sends \n, otherwise your commands will just be ignored
 */
#include <SDHash.h>

#include <avr/pgmspace.h>

static int FreeRam(void) {
  extern int  __bss_end;
  extern int* __brkval;
  int free_memory;
  if (reinterpret_cast<int>(__brkval) == 0) {
    // if no heap use from end of bss section
    free_memory = reinterpret_cast<int>(&free_memory)
                  - reinterpret_cast<int>(&__bss_end);
  } else {
    // use from top of stack to heap
    free_memory = reinterpret_cast<int>(&free_memory)
                  - reinterpret_cast<int>(__brkval);
  }
  return free_memory;
}

char inputStr[32];  
char *inputPtr;

char* tok(char* ptr) {
  if (!ptr) return ptr;
  
  while(*ptr && *ptr != ' ') ++ptr;
  if (*ptr == '\0') return NULL;
  *ptr = '\0';
  return ptr+1;
}
    
void handleError(uint8_t errorCode) {
  switch(errorCode)
  {
    case SDH_ERR_FILE_NOT_FOUND:
      Serial.println("404");
      break;
      
    case SDH_ERR_NO_SPACE:
      Serial.println("no space");
      break;
    
    case SDH_ERR_SD:
      Serial.print("sd error=0x");
      Serial.println(SDHash.sdErrorCode());
      break;
      
    case SDH_ERR_FILE_EXISTS:
      Serial.println("file already exists");
      break;
    
    case SDH_ERR_WRONG_SEGMENT_TYPE:
      Serial.println("wrong segment type");
      break;
      
    case SDH_ERR_MISSIG_SEGMENT:
      Serial.println("missing segment");
      break;

    case SDH_ERR_INVALID_ARGUMENT:
      Serial.println("invalid args");
      break;    
      
    default:
      Serial.print("error=0x");
      Serial.println(errorCode, HEX);
      break;
  }
}

void handleInput() {
  Serial.println(inputStr);
  
  char *endPtr = inputStr + strlen(inputStr);
  
  char *token = inputStr;
  char *ptr = tok(inputStr);
  
  if (strcmp(token, "i") == 0) {
    Serial.println("init'ing SD card.. ");  
    SDHash.begin();
    Serial.print("card is valid="); 
    Serial.println(SDHash.validCard());
  } else if (strcmp(token, "zmagic") == 0) {
    uint8_t ret = SDHash.zeroMagic();
    if ( ret == SDH_OK) Serial.println("ok");
    else handleError(ret);
  } else if (strcmp(token, "free") == 0) {
    Serial.print("free ram=");
    Serial.println(FreeRam());
  } else if (strcmp(token, "d") == 0) {
    if (ptr) {      
      token = ptr;
      
      Serial.print("delete=");
      Serial.println(token);

      SDHFilehandle fh = SDHash.filehandle(token);
      uint8_t ret = SDHash.deleteFile(fh);
      if (ret == SDH_OK) Serial.println("ok");
      else handleError(ret);
    }
  } else if (strcmp(token, "c") == 0) {
    if (ptr) {
      token = ptr;
      
      SDHFilehandle fh = SDHash.filehandle(token);
      
      Serial.print("creating=");
      Serial.println(token);
      
      Serial.print("hash=0x");
      Serial.println(fh, HEX);
      
      uint8_t ret = SDHash.createFile(fh, token);
      if (ret == SDH_OK) Serial.println("ok");
      else handleError(ret);
    }
  } else if (strcmp(token, "s") == 0) {
    if (ptr) {
      token = ptr;
      
      Serial.print("stat=");
      Serial.println(token);

      SDHFilehandle fh = SDHash.filehandle(token);
      
      FileInfo finfo;
      SDHAddress addr;
      uint8_t ret = SDHash.statFile(fh, &finfo, &addr);
      
      if (ret == SDH_OK) {
        Serial.print("addr=");
        Serial.println(addr);
        
        Serial.print("hash=0x");
        Serial.println(finfo.hash, HEX);
        
        Serial.print("segments=");
        Serial.println(finfo.segments_count);
      } else handleError(ret);
    }
  } else if (strcmp(token, "saddr") == 0) {
    if (ptr) {
      token = ptr;
      
      Serial.print("stat addr=");
      Serial.println(token);

      SDHAddress addr = atof(token);
      FileInfo finfo;
      uint8_t ret = SDHash.statSeg0(addr, &finfo);
      
      if (ret == SDH_OK) {
        Serial.print("hash=0x");
        Serial.println(finfo.hash, HEX);
        
        Serial.print("segments=");
        Serial.println(finfo.segments_count);
      } else handleError(ret);
    }   
  } else if (strcmp(token, "z") == 0) {
    if (ptr) {
      token = ptr;
      
      Serial.print("zero addr=");
      Serial.println(token);
      
      SDHAddress addr = atof(token);
      uint8_t ret = SDHash.zero(addr, 1);
      
      if (ret == SDH_OK) Serial.println("ok");
      else handleError(ret);
    }
  } else if (strcmp(token, "a")==0) {
    if (ptr) {
      char *filename = ptr;
      char *dataptr = NULL; 
      dataptr = tok(filename);
      
      if (dataptr) {
        Serial.print("append=");
        Serial.println(filename);
        Serial.print("data=");
        Serial.println(dataptr);
        Serial.print("length=");
        Serial.println(strlen(dataptr));
        
        SDHFilehandle fh = SDHash.filehandle(filename);
        uint8_t ret = SDHash.appendFile(fh, (uint8_t*)dataptr, strlen(dataptr));
        if (ret == SDH_OK) Serial.println("ok");
        else handleError(ret);
      }  
    }
  } else if (strcmp(token, "r16") == 0) {
    if (ptr ) {
      char *filename = ptr;
      char *offsetptr = tok(filename);
      
      if (offsetptr) {
        Serial.print("read=");
        Serial.println(filename);
        Serial.print("offset=");
        Serial.println(offsetptr);
        
        uint16_t len[1] = {16};
        uint8_t ret = SDHash.readFile(SDHash.filehandle(filename), atoi(offsetptr), (uint8_t*)inputStr, len);
        if (ret == SDH_OK) {
          Serial.print(16-len[0], DEC);
          Serial.print(" bytes=");
          for (uint8_t idx = 0; idx < 16-len[0]; ++idx) {
            Serial.print(inputStr[idx], HEX);
            Serial.print(" ");
          }
          Serial.println("");
        } else handleError(ret);
      }
    }
  } else if (strcmp(token, "segaddr") == 0) {
    if (ptr) {
      char *filename = ptr;
      char *segnumptr = tok(filename);      
      if (segnumptr) {
        SDHAddress addr;
        uint8_t ret = SDHash.findSeg(SDHash.filehandle(filename), atoi(segnumptr), &addr);
        if (ret == SDH_OK) {
          Serial.print("addr=");
          Serial.println(addr, DEC);
        } else handleError(ret);
      }
    }
  } else if (strcmp(token, "repseg") == 0) {
    if (ptr) {
      char *filename = ptr;
      char *segnumptr = tok(filename);
      char *data = tok(segnumptr);
      if (segnumptr && data) {
        Serial.print("replace=");
        Serial.println(filename);
        Serial.print("seg=");
        Serial.println(segnumptr);
        Serial.print("data=");
        Serial.println(data);
              
        uint8_t ret = SDHash.replaceSegment(SDHash.filehandle(filename), atoi(segnumptr), (uint8_t*)data, strlen(data));
        if (ret == SDH_OK) Serial.println("ok");
        else handleError(ret);
      } 
    }
  } else if (strcmp(token, "ls") == 0) {
    uint8_t buf[1+sizeof(SDHAddress)];
    uint16_t len; 
    uint8_t ret;
    uint8_t off = 0;
    do {
      len = sizeof buf;
      ret = SDHash.readFile(SDHash.filehandle("__LOG"), off, buf, &len);
      if (ret != SDH_OK) handleError(ret);
      
      if (len == 0) {
        Serial.print(buf[0]);
        Serial.print(" ");
        Serial.println(*((SDHAddress*)(buf+1)), DEC);
        
      }
      off += sizeof buf;
    } while (len == 0);
  }
}

void setup() {
  Serial.begin(9600);
  uint8_t ret =SDHash.begin();
  if (ret != SDH_OK) handleError(ret);
  
  inputPtr = inputStr;
  Serial.print("> ");
}

void loop() {
  if (Serial.available()>0){
    int ch = Serial.read();
    if (ch >= 0) {
      if (ch == '\n') ch = '\0';
      *inputPtr = ch;
      inputPtr+=1;
    }
    
    if (inputPtr - inputStr > sizeof inputStr - 1) {
      Serial.println("\nInput too long");
      inputPtr = inputStr;
    }
    else if (ch == '\0') {
      handleInput();
      
      inputPtr = inputStr;
      Serial.print("> ");
    }
  }
}
