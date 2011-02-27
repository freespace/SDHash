#include <SDHash.h>
enum {
  TEST_OK,
  TEST_FAILED,
  TEST_ERROR,
};

uint8_t _testPattern[] = {
  0x1, 
  0x2,0x2, 
  0x3,0x3,0x3, 
  0x4,0x4,0x4,0x4, 
  0x5,0x5,0x5,0x5,0x5, 
  0x6,0x6,0x6,0x6,0x6,0x6, 
  0x7,0x7,0x7,0x7,0x7,0x7,0x7, 
  0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,
  0x9,0x9,0x9,0x9,0x9,0x9,0x9,0x9,0x9
};

uint8_t test1(uint8_t *err) {
  
  char *filename = "sdhash.test";
  SDHFilehandle fh = SDHash.filehandle(filename);
  
  *err = SDHash.deleteFile(fh);
  if (*err != SDH_OK && *err != SDH_ERR_FILE_NOT_FOUND) return TEST_ERROR;
  
  *err = SDHash.createFile(fh, filename);
  if (*err != SDH_OK) return TEST_ERROR;
  
  for (byte idx = 0; idx < sizeof _testPattern; idx+=_testPattern[idx]) {
    *err = SDHash.appendFile(fh, _testPattern+idx, _testPattern[idx]);
    if (*err != SDH_OK) return TEST_ERROR;
  }

  /************************************************************************/
    
  Serial.println("testing reading across boundaries");
  uint8_t buf[sizeof _testPattern];
  SDHDataSize len = sizeof buf;
  *err = SDHash.readFile(fh, 0, buf, &len);
  if (*err != SDH_OK) return TEST_ERROR;
  
  if (len) {
    Serial.println("length mismatch");
    return TEST_FAILED;
  }
  
  if (memcmp(buf, _testPattern, sizeof buf)) {
    Serial.print("data mismatch");
    for (byte idx = 0; idx < sizeof buf; ++idx) {
      Serial.print(" 0x");
      Serial.print(buf[idx], HEX);
    }
    return TEST_FAILED; 
  }
  
  /************************************************************************/
  Serial.println("testing segment replacement");
  
  // invert the test pattern
  for (byte idx = 0; idx < sizeof _testPattern; idx+=1) {
    _testPattern[idx] = ~_testPattern[idx];
  }
  
  // now replace old test pattern with a new one
  byte segnum = 1;
  byte seg_len;
  for (byte idx = 0; idx < sizeof _testPattern; idx+=seg_len, segnum+=1) {
    seg_len = ~_testPattern[idx];
    *err = SDHash.replaceSegment(fh, segnum, _testPattern+idx, seg_len);
    if (*err != SDH_OK) return TEST_ERROR;
  }
  
  len = sizeof buf;
  *err = SDHash.readFile(fh, 0, buf, &len);
  if (*err != SDH_OK) return TEST_ERROR;
  
  if (len) {
    Serial.println("length mismatch");
    return TEST_FAILED;
  }
  
  if (memcmp(buf, _testPattern, sizeof buf)) {
    Serial.print("data mismatch");
    return TEST_FAILED; 
  }
  /************************************************************************/
  
  Serial.println("testing truncation");
    
  *err = SDHash.truncateFile(fh, 1);
  if (*err != SDH_OK) return *err;
  
  memset(buf, 0xFF, sizeof buf);
  
  len = sizeof buf;
  *err = SDHash.readFile(fh, 0, buf, &len);
  if (*err != SDH_OK) return TEST_ERROR;
  
  if (len!=0x09) {
    Serial.print("length mismatch=");
    Serial.println(len, DEC);
  
    return TEST_FAILED;
  }
  
  if (memcmp(buf, _testPattern, sizeof buf-0x09)) {
    Serial.print("data mismatch");
    return TEST_FAILED;
  }
  
  return TEST_OK;
}

void printError(uint8_t err) {
  Serial.print(err, HEX);
  Serial.print(" ");
  char* str = "unknown error";
  switch(err) {
  }
  
  Serial.print("err: ");
  Serial.println(str);
}

void setup() {
  Serial.begin(9600);
  
  SDHash.begin();
  SDHash.zeroMagic();
  SDHash.begin();
  
  Serial.println("running tests...");
  
  uint8_t err;
  
  switch(test1(&err)) {
    case TEST_OK:
      break;
    case TEST_ERROR:
      printError(err);
    case TEST_FAILED:
      return;
  }
      
  Serial.println("all tests passed");
}

void loop () {
}
