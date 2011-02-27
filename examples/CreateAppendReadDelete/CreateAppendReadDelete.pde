#include <SDHash.h>

void setup() {
  SDHash.begin();
  
  char *filename = "steve";
  
  uint32_t fh = SDHash.filehandle(filename);
  SDHash.createFile(fh, filename);
  SDHash.statFile(fh, NULL, NULL);
  SDHash.appendFile(fh, (uint8_t*)filename, strlen(filename));
  uint8_t buf[16];
  uint16_t len = sizeof buf;
  SDHash.readFile(fh, 0, buf, &len);
  SDHash.deleteFile(fh);
}

void loop () {
};
