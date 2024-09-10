#include <Arduino.h>
#include <painlessMesh.h>
#include <Crypto.h>
#include <AES.h>
#include <queue>
#include <vector>
#include <ArduinoJson.h>  //v7
#include <uECC.h>

Scheduler userScheduler;
painlessMesh mesh;
#define MESH_PREFIX "Hieroglossa"
#define MESH_PASSWORD "Oneirodyne"
#define MESH_PORT 5555

#define IV_LENGTH 16
String key = "0123456789ABCDEF0123456789ABCDEF";
struct Message {
  uint32_t recipient;
  String content;
  uint8_t sendAttempts;
  bool ack;
};

AESTiny128 aes128;
byte aes_key[16];
byte iv[IV_LENGTH];

struct cipher {
  String iv;
  String hx;
};

uECC_Curve curve = uECC_secp160r1();
byte privateKey[21];
byte publicKey[42];
std::map<uint32_t, std::vector<byte>> sharedKeys;

std::vector<Message> messageQueue;
TaskHandle_t messageTaskHandle;

// New definitions for key management
#define MAX_NODES 100
String nodeKeys[MAX_NODES];
uint32_t nodeIds[MAX_NODES];
int nodeCount = 0;

// Function prototypes
void sendMessageTask(void* parameter);
void receivedCallback(uint32_t from, String& msg);
void ECDH_NewConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int adjustedTime);
void SerialReadLine(void* parameter);
String encryptAES(const String& plaintext, byte* sharedKey);
String decryptAES(const String& ciphertext, byte* sharedKey);
String padString(String input, size_t size);
void generateECDHKeys();
byte* randomize_IV(byte* iv);
void handleKeyExchange(uint32_t from, const String& msg);
String byteArrayToHexString(const byte* byteArray, size_t length);
void hexStringToByteArray(const String& hexString, byte* byteArray, size_t length);

void performLocalEncryptionTest(const String& plaintext);
void saveNodeKey(uint32_t nodeId, const String& key);
String getNodeKey(uint32_t nodeId);
void sendSecureMessage(uint32_t nodeId, const String& payload);
cipher encrypt(String raw);
String decrypt(cipher cf);

// Add these new function declarations at the top of the file
void performLatencyTest(uint32_t recipient, int payloadLength);
String generateRandomPayload(int length);

// Add this new function to generate a random payload
String generateRandomPayload(int length) {
  String payload = "";
  for (int i = 0; i < length; i++) {
    payload += (char)(random(32, 127));  // Printable ASCII characters
  }
  return payload;
}


void removeBackslashes(String& str) {
  for (int i = 0; i < str.length(); i++) {
    if (str.charAt(i) == '\\') {
      str.remove(i, 1);
      i--;
    }
  }
}

String byteArrayToString(byte* byteArray, size_t size) {
  String result;
  for (size_t i = 0; i < size; i++) {
    result += (char)byteArray[i];
  }
  return result;
}

String StringRawToStringHex(String str) {
  String rtr;
  for (int i = 0; i < str.length(); ++i) {
    char hex[3];
    sprintf(hex, "%02X", str[i]);
    rtr += hex;
  }
  return rtr;
}

String ByteArrayToStringHex(uint8_t* byteArray, size_t size) {
  String rtr;
  for (int i = 0; i < size; ++i) {
    char hex[3];
    sprintf(hex, "%02X", byteArray[i]);
    rtr += hex;
  }
  return rtr;
}

void printByteArray(const uint8_t* byteArray, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    Serial.print(byteArray[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void hexStringToByteArray(const String& hexString, uint8_t* byteArray, size_t arraySize) {
  for (size_t i = 0; i < arraySize; ++i) {
    String hexByte = hexString.substring(i * 2, i * 2 + 2);
    byteArray[i] = strtol(hexByte.c_str(), NULL, 16);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hieroglossa Development Project v.1.2");

  Serial.println("Configuring Mesh");
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&ECDH_NewConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  Serial.println("Node ID: " + String(mesh.getNodeId()));

  Serial.println("Configuring FreeRTOS");
  xTaskCreate(SerialReadLine, "SerialReadLine", 65536, NULL, 1, NULL);
  xTaskCreate(sendMessageTask, "SendMessageTask", 131072, NULL, 2, &messageTaskHandle);

  Serial.println("Generating ECDH keys");
  generateECDHKeys();

  // Initialize nodeIds array
  for (int i = 0; i < MAX_NODES; i++) {
    nodeIds[i] = 0;
  }

  Serial.println("Finished Setup.");
}

void loop() {
  mesh.update();
}

void sendMessageTask(void* parameter) {
  while (true) {
    if (!messageQueue.empty()) {
      for (size_t i = 0; i < messageQueue.size(); ++i) {
        Message& msg = messageQueue[i];
        bool success = false;
        if (msg.recipient > 0) {
          success = mesh.sendSingle(msg.recipient, msg.content);
          Serial.print(">");
        } else if (msg.recipient == -1) {
          success = mesh.sendBroadcast(msg.content);
          Serial.print("]");
        }

        if (success) {
          msg.sendAttempts++;
          if (msg.sendAttempts >= 10 || !msg.ack) {
            messageQueue.erase(messageQueue.begin() + i);
            i--;
          }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
      }
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// Global variables for latency test
String latencyTestResponse = "";
bool latencyTestResponseReceived = false;

void receivedCallback(uint32_t from, String& msg) {
  Serial.printf("Received from %u msg=%s\n", from, msg.c_str());

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, msg);

  if (error) {
    Serial.println("Failed to parse received message");
    return;
  }

  if (doc.containsKey("type")) {
    String messageType = doc["type"];

    if (messageType == "latencyTest") {
      // Handle latency test message
      Serial.println("Received latency test message");
      doc["type"] = "latencyTestReflection";
      String reflectionMsg;
      serializeJson(doc, reflectionMsg);
      Serial.println("OUTBOUND");
      Serial.println("Reflecting latency test message");
      mesh.sendSingle(from, reflectionMsg);
    } else if (messageType == "latencyTestReflection") {
      // Store the response and set the flag
      latencyTestResponse = msg;
      latencyTestResponseReceived = true;
      Serial.println("Received latency test reflection");
    } else if (messageType == "keyExchange") {
      handleKeyExchange(from, msg);
    } else if (messageType == "encryptedMessage") {
      String encryptedContent = doc["content"];
      String key = getNodeKey(from);
      if (key.length() > 0) {
        byte aesKey[16];
        hexStringToByteArray(key, aesKey, 16);
        String decryptedMsg = decryptAES(encryptedContent, aesKey);

        DynamicJsonDocument contentDoc(512);
        DeserializationError contentError = deserializeJson(contentDoc, decryptedMsg);
        if (!contentError) {
          String payload = contentDoc["payload"];
          Serial.println("Decrypted message from " + String(from) + ": " + payload);
        } else {
          Serial.println("Failed to parse decrypted message");
        }
      } else {
        Serial.println("No shared key found for node " + String(from));
      }
    } else if (messageType == "throughputTest") {
      // Handle throughput test message
      String payload = doc["payload"];
      int messageNumber = doc["message_number"];
      Serial.printf("Received throughput test message %d: %s\n", messageNumber, payload.c_str());

      // You might want to add some processing or reflection logic here
    } else {
      Serial.println("Unknown message type: " + messageType);
    }
  } else {
    // Handle messages without a type (e.g., plain text messages)
    Serial.println("Received message without type: " + msg);
  }

  Serial.println("INBOUND");
  Serial.println(msg);
}
void ECDH_NewConnectionCallback(uint32_t nodeId) {
  Serial.println("New connection, nodeId = " + String(nodeId));
  changedConnectionCallback();
  DynamicJsonDocument doc(200);
  doc["type"] = "keyExchange";
  doc["publicKey"] = byteArrayToHexString(publicKey, sizeof(publicKey));
  String msg;
  serializeJson(doc, msg);
  mesh.sendSingle(nodeId, msg);
}

void changedConnectionCallback() {
  Serial.println("Changed connections");
  Serial.println("Current mesh topology:");
  // Get the sub-connection JSON
  String topologyJson = mesh.subConnectionJson();
  Serial.println(topologyJson);
}

void nodeTimeAdjustedCallback(int adjustedTime) {
  //Serial.println("Node time adjusted: " + String(adjustedTime));
}



// Structure to hold test parameters
struct ThroughputTestParams {
  int recipient;
  String payload;
  int freq;
  int iterations;
};

// Task function
void throughputTestTask(void* pvParameters) {
  ThroughputTestParams* params = (ThroughputTestParams*)pvParameters;

  int recipient = params->recipient;
  String payload = params->payload;
  int freq = params->freq;
  int iterations = params->iterations;

  // Calculate delay between messages in milliseconds
  unsigned long delay_ms = 1000 / freq;

  Serial.printf("Starting throughput test: recipient=%d, payload='%s', freq=%d Hz, iterations=%d\n",
                recipient, payload.c_str(), freq, iterations);

  unsigned long start_time = millis();
  unsigned long next_send_time = start_time;

  for (int i = 1; i <= iterations; i++) {
    // Create JSON object
    StaticJsonDocument<200> doc;
    doc["message_number"] = i;
    doc["payload"] = payload;

    // Serialize JSON to string
    String jsonString;
    serializeJson(doc, jsonString);

    // Send the message
    mesh.sendSingle(recipient, jsonString);

    // Print sent message and time (for debugging)
    Serial.printf("Sent message %d at %lu ms: %s\n", i, millis() - start_time, jsonString.c_str());

    // Calculate next send time
    next_send_time += delay_ms;

    // Wait until it's time to send the next message
    while (millis() < next_send_time) {
      // Allow other tasks to run
      vTaskDelay(1);
    }

    // Print a debug message every 10 iterations
    if (i % 10 == 0) {
      Serial.printf("Completed %d iterations at %lu ms\n", i, millis() - start_time);
    }
  }

  unsigned long end_time = millis();
  unsigned long total_time = end_time - start_time;
  float actual_freq = (float)iterations / (total_time / 1000.0);

  Serial.printf("Throughput test completed in %lu ms\n", total_time);
  Serial.printf("Actual frequency: %.2f Hz\n", actual_freq);

  // Free the allocated memory
  delete params;

  // Delete this task
  vTaskDelete(NULL);
}

// Wrapper function to start the throughput test task
void test_throughput(int recipient, String payload, int freq, int iterations) {
  ThroughputTestParams* params = new ThroughputTestParams{ recipient, payload, freq, iterations };

  xTaskCreate(
    throughputTestTask,  // Task function
    "ThroughputTest",    // Task name
    10000,               // Stack size (bytes)
    params,              // Task parameters
    1,                   // Task priority
    NULL                 // Task handle
  );

  Serial.println("Throughput test task created");
}

void performLatencyTest(uint32_t recipient, int payloadLength) {
  String payload = generateRandomPayload(payloadLength);

  DynamicJsonDocument doc(512);
  doc["type"] = "latencyTest";
  doc["payload"] = payload;

  String jsonMessage;
  serializeJson(doc, jsonMessage);

  // Reset global variables
  latencyTestResponse = "";
  latencyTestResponseReceived = false;

  unsigned long startTime = micros();

  if (mesh.sendSingle(recipient, jsonMessage)) {
    Serial.println("Latency test message sent to node " + String(recipient));
  } else {
    Serial.println("Failed to send latency test message to node " + String(recipient));
    return;
  }

  // Wait for the response
  unsigned long timeout = 5000000;  // 5 seconds timeout
  unsigned long startWait = micros();
  while (micros() - startWait < timeout) {
    mesh.update();
    if (latencyTestResponseReceived) {
      DynamicJsonDocument receivedDoc(512);
      DeserializationError error = deserializeJson(receivedDoc, latencyTestResponse);
      if (!error) {
        if (receivedDoc["type"] == "latencyTestReflection" && receivedDoc["payload"] == payload) {
          unsigned long endTime = micros();
          unsigned long latency = endTime - startTime;
          Serial.printf("Round-trip latency /us: %lu\n", latency);
          return;
        }
      }
      // Reset for next potential message
      latencyTestResponseReceived = false;
    }
    yield();
  }
  Serial.println("Latency test timed out");
}

void SerialReadLine(void* parameter) {
  String input = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        if (input.length() > 0) {
          // Parse the command
          if (input.startsWith("hirg ")) {
            String args = input.substring(5);  // Remove "hirg " from the beginning
            int recipientIndex = args.indexOf("-r ");
            int secureIndex = args.indexOf("-s ");
            int plaintextIndex = args.indexOf("-p ");
            int throughputIndex = args.indexOf("-t ");
            int iterationsIndex = args.indexOf("-i ");
            int latencyIndex = args.indexOf("-l ");
            int encryptTestIndex = args.indexOf("-e ");

            if (encryptTestIndex != -1) {
              // Local encryption test
              String plaintext = args.substring(encryptTestIndex + 3);
              plaintext.trim();
              Serial.println("Starting local encryption test");
              performLocalEncryptionTest(plaintext);
            } else if (recipientIndex != -1) {
              // Extract recipient (node ID)
              String recipientStr = args.substring(recipientIndex + 3);
              int nextFlagIndex = recipientStr.indexOf('-');
              if (nextFlagIndex != -1) {
                recipientStr = recipientStr.substring(0, nextFlagIndex);
              }
              recipientStr.trim();
              uint32_t recipient = strtoul(recipientStr.c_str(), NULL, 10);

              if (latencyIndex != -1) {
                // Latency test
                String lengthStr = args.substring(latencyIndex + 3);
                lengthStr.trim();
                int payloadLength = lengthStr.toInt();

                Serial.println("Starting latency test");
                performLatencyTest(recipient, payloadLength);
              } else if (plaintextIndex != -1) {
                // Plaintext message
                String payload;
                int payloadEnd = (throughputIndex != -1) ? throughputIndex : args.length();
                payload = args.substring(plaintextIndex + 3, payloadEnd);
                payload.trim();

                if (throughputIndex != -1 && iterationsIndex != -1) {
                  // Throughput test
                  String freqStr = args.substring(throughputIndex + 3, iterationsIndex);
                  freqStr.trim();
                  int freq = freqStr.toInt();

                  String iterStr = args.substring(iterationsIndex + 3);
                  iterStr.trim();
                  int iterations = iterStr.toInt();

                  Serial.println("Starting throughput test task");
                  test_throughput(recipient, payload, freq, iterations);
                } else {
                  Serial.println("OUTBOUND");
                  Serial.println("Sending plaintext message");
                  mesh.sendSingle(recipient, payload);
                }
              } else if (secureIndex != -1) {
                // Secure message
                String payload = args.substring(secureIndex + 3);
                payload.trim();

                Serial.println("OUTBOUND");
                Serial.println("Sending secure message");
                sendSecureMessage(recipient, payload);
              } else {
                Serial.println("Invalid command format. Use: hirg -r <nodeid> [-s <payload> | -p <payload> [-t <frequency> -i <iterations>] | -l <random payload length>]");
              }
            } else {
              Serial.println("Invalid command format. Recipient (-r) is required.");
            }
          } else {
            Serial.println("Unknown command. Use: hirg -r <nodeid> [-s <payload> | -p <payload> [-t <frequency> -i <iterations>] | -l <random payload length>]");
          }
          input = "";
        }
      } else {
        input += c;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}


void performLocalEncryptionTest(const String& plaintext) {
  Serial.println("\nStarting AES Test *********************");
  // Generate a random key for testing
  byte testKey[16];
  for (int i = 0; i < 16; ++i) {
    testKey[i] = random(256);
  }
  
  Serial.println("Test Key: " + byteArrayToHexString(testKey, 16));
  String encryptedText = encryptAES(plaintext, testKey);
  Serial.println("Encrypted: " + encryptedText);
  String decryptedText = decryptAES(encryptedText, testKey);
  Serial.println("Decrypted: " + decryptedText);
  // Verify
  if (decryptedText == plaintext) {
    Serial.println("Encryption/Decryption successful");
  } else {
    Serial.println("Error: Decrypted text does not match original plaintext.");
  }
}

String randomize_iv(uint8_t* iv) {
  randomSeed(analogRead(35));
  String init_vector_str;
  for (int i = 0; i < 16; i++) {
    iv[i] = random(256);
  }
  init_vector_str = ByteArrayToStringHex(iv, 16);
  Serial.println("Initialization Vector shuffled: " + init_vector_str);
  return init_vector_str;
}

String pkcs7pad(String plaintext) {
  int paddingLength = 16 - (plaintext.length() % 16);
  for (int i = 0; i < paddingLength; ++i) {
    plaintext += (char)paddingLength;
  }
  return plaintext;
}

String pkcs7unpad(String paddedText) {
  int paddingLength = paddedText[paddedText.length() - 1];
  return paddedText.substring(0, paddedText.length() - paddingLength);
}

String encryptAES(const String& plaintext, byte* sharedKey) {
  key = String((char*)sharedKey);
  cipher cf = encrypt(plaintext);
  return cf.iv + cf.hx;
}

cipher encrypt(String raw) {
  String ciphertext = "";
  uint8_t buffer[16];
  unsigned int retry = 0;
  uint8_t iv[16] = {};
  String iv_encrypt = randomize_iv(iv);
  unsigned long time_elapsed = micros();
  raw.trim();
  Serial.println(raw);

  bool encryption_error = 0;

  do {
    String content = pkcs7pad(raw);
    ciphertext = "";

    while (content.length() > 0) {
      String plaintext = content.substring(0, 16);
      content = content.substring(16);

      aes128.setKey(reinterpret_cast<const uint8_t*>(key.c_str()), 16);

      for (int i = 0; i < 16; ++i) {
        buffer[i] = plaintext[i] ^ iv[i];
      }

      aes128.encryptBlock(buffer, buffer);

      for (int i = 0; i < 16; ++i) {
        buffer[i] ^= iv[i];
        ciphertext += String((char)buffer[i]);
      }
    }

    if (ciphertext.length() % 16 != 0) {
      encryption_error = 1;
      Serial.println("Error: Ciphertext length mismatch detected, retry.");
      iv_encrypt = randomize_iv(iv);
      retry++;
      if (retry > 50) {
        Serial.println("Encryption failed.");
      }
    } else {
      encryption_error = 0;
    }
  } while (encryption_error == 1);

  Serial.println("Plaintext Length: " + String(raw.length()));
  Serial.println("Ciphertext Length: " + String(ciphertext.length()));
  time_elapsed = micros() - time_elapsed;
  Serial.println("Encryption time /us: " + String(time_elapsed));

  String hexString = StringRawToStringHex(ciphertext);
  Serial.println(hexString);

  cipher rtr;
  rtr.iv = iv_encrypt;
  rtr.hx = hexString;

  return rtr;
}

String decryptAES(const String& ciphertext, byte* sharedKey) {
  String iv_hex = ciphertext.substring(0, 32);
  String ciphertext_hex = ciphertext.substring(32);

  cipher cf;
  cf.iv = iv_hex;
  cf.hx = ciphertext_hex;

  key = String((char*)sharedKey);

  return decrypt(cf);
}

String decrypt(cipher cf) {
  String iv_decrypt = cf.iv;
  String hexString = cf.hx;
  uint8_t buffer[16];
  String cipher = "";
  String result = "";
  unsigned long time_elapsed = micros();
  byte iv[16];
  hexStringToByteArray(iv_decrypt, iv, 16);
  Serial.println("IV: " + byteArrayToHexString(iv, 16));
  Serial.println("Cipher: " + hexString);

  for (int i = 0; i < hexString.length(); i += 2) {
    String hexByte = hexString.substring(i, i + 2);
    char c = strtol(hexByte.c_str(), NULL, 16);
    cipher += c;
  }
  Serial.println("Ciphertext Length: " + String(cipher.length()));

  while (cipher.length() > 0) {
    String plaintext = cipher.substring(0, 16);
    cipher = cipher.substring(16);

    aes128.setKey(reinterpret_cast<const uint8_t*>(key.c_str()), 16);
    for (int i = 0; i < 16; ++i) {
      buffer[i] = plaintext[i] ^ iv[i];
    }
    aes128.decryptBlock(buffer, buffer);
    for (int i = 0; i < 16; ++i) {
      buffer[i] ^= iv[i];
      result += String((char)buffer[i]);
    }
  }

  result = pkcs7unpad(result);
  time_elapsed = micros() - time_elapsed;
  Serial.println("Decryption result: " + result);
  Serial.println("Decryption time /us: " + String(time_elapsed));
  return result;
}

String padString(String input, size_t size) {
  while (input.length() % size != 0) {
    input += ' ';
  }
  return input;
}

int getRandomNumber() {
  return random(0, 256);
}

int generateRandomBytes(uint8_t* dest, unsigned size) {
  for (unsigned i = 0; i < size; ++i) {
    dest[i] = getRandomNumber();
  }
  return 1;
}

void generateECDHKeys() {
  uECC_set_rng(&generateRandomBytes);

  if (!uECC_make_key(publicKey, privateKey, curve)) {
    Serial.println("Failed to generate ECDH keys");
  } else {
    Serial.println("ECDH keys generated");
    Serial.println("Public Key: " + byteArrayToHexString(publicKey, sizeof(publicKey)));
    Serial.println("Private Key: " + byteArrayToHexString(privateKey, sizeof(privateKey)));
  }
}

void handleKeyExchange(uint32_t from, const String& msg) {
  DynamicJsonDocument doc(200);
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.println("Failed to parse key exchange message");
    return;
  }

  if (doc.containsKey("publicKey")) {
    String publicKeyHex = doc["publicKey"];
    byte receivedPublicKey[42];
    hexStringToByteArray(publicKeyHex, receivedPublicKey, sizeof(receivedPublicKey));

    byte sharedSecret[21];
    if (!uECC_shared_secret(receivedPublicKey, privateKey, sharedSecret, curve)) {
      Serial.println("Failed to generate shared secret");
      return;
    }

    byte aesKey[16];
    memcpy(aesKey, sharedSecret, 16);

    String aesKeyHex = byteArrayToHexString(aesKey, 16);
    saveNodeKey(from, aesKeyHex);
  }
}

String byteArrayToHexString(const byte* byteArray, size_t length) {
  String hexString;
  for (size_t i = 0; i < length; i++) {
    if (byteArray[i] < 0x10) hexString += "0";
    hexString += String(byteArray[i], HEX);
  }
  return hexString;
}

void saveNodeKey(uint32_t nodeId, const String& key) {
  for (int i = 0; i < nodeCount; i++) {
    if (nodeIds[i] == nodeId) {
      nodeKeys[i] = key;
      return;
    }
  }

  if (nodeCount < MAX_NODES) {
    nodeIds[nodeCount] = nodeId;
    nodeKeys[nodeCount] = key;
    nodeCount++;
  } else {
    Serial.println("Error: Maximum number of nodes reached.");
  }
}

String getNodeKey(uint32_t nodeId) {
  for (int i = 0; i < nodeCount; i++) {
    if (nodeIds[i] == nodeId) {
      return nodeKeys[i];
    }
  }
  return "";
}

void sendSecureMessage(uint32_t nodeId, const String& payload) {
  String key = getNodeKey(nodeId);
  if (key.length() == 0) {
    Serial.println("Error: No key found for node " + String(nodeId));
    return;
  }

  byte aesKey[16];
  hexStringToByteArray(key, aesKey, 16);

  DynamicJsonDocument doc(512);
  doc["type"] = "secureMessage";
  doc["sender"] = mesh.getNodeId();
  doc["payload"] = payload;

  String jsonMessage;
  serializeJson(doc, jsonMessage);

  String encryptedMessage = encryptAES(jsonMessage, aesKey);

  DynamicJsonDocument sendDoc(1024);
  sendDoc["type"] = "encryptedMessage";
  sendDoc["content"] = encryptedMessage;

  String finalMessage;
  serializeJson(sendDoc, finalMessage);

  if (mesh.sendSingle(nodeId, finalMessage)) {
    Serial.println("Secure message sent to node " + String(nodeId));
  } else {
    Serial.println("Failed to send secure message to node " + String(nodeId));
  }
}