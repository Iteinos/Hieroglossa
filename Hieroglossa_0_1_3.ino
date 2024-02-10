#include <Crypto.h>  //Arduino Cryptography Library
#include <AES.h>
#include <uECC.h>  //Elliptical Curvature Cryptography
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <soc/rtc_wdt.h>
void removeBackslashes(String &str) {
  for (int i = 0; i < str.length(); i++) {
    if (str.charAt(i) == '\\') {
      str.remove(i, 1);
      i--;  // Decrement i to recheck the current position since characters have shifted left
    }
  }
}

String byteArrayToString(byte *byteArray, size_t size) {
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
String ByteArrayToStringHex(uint8_t *byteArray, size_t size) {
  String rtr;
  for (int i = 0; i < size; ++i) {
    char hex[3];
    sprintf(hex, "%02X", byteArray[i]);
    rtr += hex;
  }
  return rtr;
}
void printByteArray(const uint8_t *byteArray, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    Serial.print(byteArray[i], HEX);
    Serial.print(" ");
  }
  Serial.println();  // Print a newline after the array
}

void hexStringToByteArray(const String &hexString, uint8_t *byteArray, size_t arraySize) {
  for (size_t i = 0; i < arraySize; ++i) {
    // Extract two characters from the hex string
    String hexByte = hexString.substring(i * 2, i * 2 + 2);

    // Convert the substring to a long (base 16) and store it in the byte array
    byteArray[i] = strtol(hexByte.c_str(), NULL, 16);
  }
}
#include <ArduinoJson.h>  //v7

JsonDocument Hieroglossa_message;
// JsonDocument PSRAM_document;

AESTiny128 aes128;

String key = "0123456789ABCDEF0123456789ABCDEF";
JsonDocument keyring;  //AES Key of every node connected
unsigned long time_elapsed;
String encryption_result_str;

String randomize_iv(uint8_t *iv) {
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

String encrypt(String raw) {
  String cipher = "";
  String result = "";
  uint8_t buffer[16];
  unsigned int retry = 0;
  uint8_t iv[16] = {};  // stack smashing bug due to size discrepancy
  String iv_encrypt = randomize_iv(iv);
  time_elapsed = micros();
  raw.trim();
  Serial.println(raw);
  // PKCS7 Padding
  bool encryption_error = 0;
  // Encryption
  do {
    String content = pkcs7pad(raw);
    cipher = "";
    result = "";
    while (content.length() > 0) {
      String plaintext = content.substring(0, 16);
      content = content.substring(16);
      //Serial.println("Encrypt using AES Key: " + key);
      aes128.setKey(reinterpret_cast<const uint8_t *>(key.c_str()), 16);
      // aes128.setKey(key, 16);
      for (int i = 0; i < 16; ++i) {
        buffer[i] = plaintext[i] ^ iv[i];
      }
      aes128.encryptBlock(buffer, buffer);
      for (int i = 0; i < 16; ++i) {
        buffer[i] ^= iv[i];
        cipher += String((char)buffer[i]);
      }
    }
    if (cipher.length() % 16 != 0) {
      encryption_error = 1;
      Serial.println("Error: Cipher length mismatch detected, retry.");  // the cause of cipher length is sometimes not divisable by 16 is unknown. Retrying forever will resolve the issue.
      iv_encrypt = randomize_iv(iv);
      retry++;
      if (retry > 50) {
        Serial.println("Encryption failed.");
        return "NULL";
      }
    } else {
      encryption_error = 0;
    }
  } while (encryption_error == 1);
  // End of Encryption
  Serial.println("Plaintext Length: " + String(raw.length()));
  Serial.println("Ciphertext Length: " + String(cipher.length()));
  time_elapsed = micros() - time_elapsed;
  Serial.println("Encryption time /us: " + String(time_elapsed));
  // Convert to hex
  String hexString = StringRawToStringHex(cipher);
  Serial.println(hexString);
  Hieroglossa_message["IV"] = iv_encrypt;
  Hieroglossa_message["Payload"] = hexString;
  serializeJson(Hieroglossa_message, encryption_result_str);
  Serial.println(encryption_result_str);
  if (Hieroglossa_message.overflowed())
    Serial.println("ERROR: Not enough memory to store the entire document");
  return encryption_result_str;
}

// AES Decrypt
String decrypt(String msg) {
  Serial.println("Decryption Raw: " + msg);
  uint8_t buffer[16];
  deserializeJson(Hieroglossa_message, msg);
  String iv_decrypt = Hieroglossa_message["IV"];
  String hexString = Hieroglossa_message["Payload"];
  String cipher = "";
  String result = "";
  time_elapsed = micros();
  byte iv[16];
  hexStringToByteArray(iv_decrypt, iv, 16);
  Serial.println("IV: " + ByteArrayToStringHex(iv, 16));
  Serial.println("Cipher: " + hexString);
  for (int i = 0; i < hexString.length(); i += 2) {
    String hexByte = hexString.substring(i, i + 2);
    char c = strtol(hexByte.c_str(), NULL, 16);
    cipher += c;
  }
  Serial.println("Ciphertext Length: " + String(cipher.length()));
  // Decryption
  while (cipher.length() > 0) {
    String plaintext = cipher.substring(0, 16);
    cipher = cipher.substring(16);
    //Serial.println("Decrypt using AES Key: " + key);
    aes128.setKey(reinterpret_cast<const uint8_t *>(key.c_str()), 16);
    for (int i = 0; i < 16; ++i) {
      buffer[i] = plaintext[i] ^ iv[i];
    }
    aes128.decryptBlock(buffer, buffer);
    for (int i = 0; i < 16; ++i) {
      buffer[i] ^= iv[i];
      result += String((char)buffer[i]);
    }
  }
  // PKCS7 Unpadding
  result = pkcs7unpad(result);
  // metrics
  time_elapsed = micros() - time_elapsed;
  Serial.println("Decryption result: " + result);
  Serial.println("Decryption time /us: " + String(time_elapsed));
  return result;
}

// Mesh
#include "painlessMesh.h"
#define MESH_PREFIX "Hieroglossa"
#define MESH_PASSWORD "Hieroglossa"
#define MESH_PORT 5555
painlessMesh mesh;
Scheduler userScheduler;

// ECDH
const struct uECC_Curve_t *curve = uECC_secp160r1();
uint8_t private_key[21];
uint8_t public_key[40];
uint8_t recipient_public_key[40];
uint8_t secret[20];
String ECDH_String;
uint32_t ECDH_exchange_pending_id = 0;
unsigned int AES_friendliness_pending_id = 0;

static int RNG(uint8_t *dest, unsigned size) {
  // Use the least-significant bits from the ADC for an unconnected pin (or connected to a source of
  // random noise). This can take a long time to generate random data if the result of analogRead(0)
  // doesn't change very frequently.
  while (size) {
    uint8_t val = 0;
    for (unsigned i = 0; i < 8; ++i) {
      int init = analogRead(35);
      int count = 0;
      while (analogRead(35) + random(-100, 100) == init) {
        ++count;
      }

      if (count == 0) {
        val = (val << 1) | (init & 0x01);
      } else {
        val = (val << 1) | (count & 0x01);
      }
    }
    *dest = val;
    ++dest;
    --size;
  }
  // NOTE: it would be a good idea to hash the resulting random data using SHA-256 or similar.
  return 1;
}

void ECDH_initiate_exchange(uint8_t *public_key_p, size_t size) {
  Serial.println("Initiate ECDH Exchange.");
  JsonDocument ECDH_Message;
  String ECDH_hex = ByteArrayToStringHex(public_key_p, size);
  ECDH_Message["Intention"] = "ECDH_Exchange";
  ECDH_Message["ECDH_Key"] = ECDH_hex;
  serializeJson(ECDH_Message, ECDH_String);
  Serial.println(ECDH_String);
  Serial.println("Pinging stranger..");
}

void ECDH_reciprocate_exchange(String ECDH_received_String) {
  JsonDocument ECDH_received_Message;
  deserializeJson(ECDH_received_Message, ECDH_received_String);
  String recipient_public_key_str = ECDH_received_Message["ECDH_Key"];
  Serial.println(recipient_public_key_str);
  hexStringToByteArray(recipient_public_key_str, recipient_public_key, recipient_public_key_str.length());
  Serial.println("Finishing ECDH Exchange.");
  uint32_t i = 0;
  while (!ECDH_compute_key()) {
    i++;
    if (i > 20) {
      Serial.println("ECDH Transaction Failed.");
      break;
    }
  }
}
// Function to perform ECDH key exchange
void ECDH_generate_key() {
  Serial.println("Generating ECC Key.");
  // Generate the random public-private key pair
  uECC_make_key(public_key, private_key, curve);
  Serial.println("Generated ECC key pair: \nECC Public: " + ByteArrayToStringHex(public_key, sizeof(public_key)) + "\nECC Private: " + ByteArrayToStringHex(private_key, sizeof(private_key)));
  Serial.println();
}
bool ECDH_compute_key() {
  // Calculate shared key
  if (!uECC_shared_secret(recipient_public_key, private_key, secret, curve)) {
    Serial.println("Error: failed computing a shared AES key, retry.");
    return 0;
  } else {
    key = ByteArrayToStringHex(secret, sizeof(secret));
    Serial.println("\nNode ID " + String(ECDH_exchange_pending_id) + ": AES key acquired via ECDH: " + key);
    keyring[String(ECDH_exchange_pending_id)] = key;
    ECDH_exchange_pending_id = 0;
    return 1;
  }
}

void received_JSON_parser(uint32_t sender, String &received_JSON_message) {
  JsonDocument received_JSON_document;
  deserializeJson(received_JSON_document, received_JSON_message);
  Serial.println("Received Raw: " + received_JSON_message);
  if (received_JSON_document["Intention"] == "ECDH_Exchange") {
    if (ECDH_exchange_pending_id == sender && sender != 0) {
      Serial.println("\nProcessing Message.");
      ECDH_reciprocate_exchange(received_JSON_message);
      AES_friendliness_pending_id = sender;
    }
  } else if (received_JSON_document["Intention"] == "AES_Friendliness_test" && AES_friendliness_pending_id > 0) {
    String encrypted_payload;
    JsonDocument encryption_literal;
    encryption_literal["IV"] = received_JSON_document["AES_Key"]["IV"];
    encryption_literal["Payload"] = received_JSON_document["AES_Key"]["Payload"];
    serializeJson(encryption_literal, encrypted_payload);
    Serial.println("Received Payload Raw: " + encrypted_payload);
    if (decrypt(encrypted_payload) == key) {
      Serial.println("ECDH Exchange Successful.");
      String key_id = String(AES_friendliness_pending_id);
      keyring[key_id] = key;
      Serial.println("Secure channel established with Node " + key_id);
      Serial.println("Key " + key + " saved to keyring for Node " + key_id);
      AES_friendliness_pending_id = 0;
    } else Serial.println("Not a match.");
  } else {
    Serial.println("Message ignored.");
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.println("Connected with node: " + String(nodeId));
  String savedCredentials = keyring[String(nodeId)];
  Serial.println("Saved Credentials: " + savedCredentials);
  if (savedCredentials != "null") {
    Serial.println("Node is a friend.");
    AES_friendliness_pending_id = nodeId;
  } else {
    Serial.println("Node is a stranger.");
    ECDH_generate_key();
    ECDH_exchange_pending_id = nodeId;
    ECDH_initiate_exchange(public_key, sizeof(public_key));
  }
}

void changedConnectionCallback() {
  Serial.println("Connection Changed.");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.println("Timestamp adjusted to: "+String(mesh.getNodeTime()));
}

void RTOS_mesh_update(void *parameters) {
  while (1) {
    vTaskDelay(50 / portTICK_PERIOD_MS);
    mesh.update();
  }
}

void RTOS_feed_watchdog_timer(void *parameters) {
  while (1) {
    esp_task_wdt_feed();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void RTOS_ECDH_handler(void *parameters) {
  unsigned int ECDH_RETRY = 0;
  unsigned int AES_RETRY = 0;
  while (1) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (ECDH_exchange_pending_id > 0) {
      mesh.sendSingle(ECDH_exchange_pending_id, ECDH_String);
      Serial.print(".");
      ECDH_RETRY++;
      if (ECDH_RETRY > 20) {
        Serial.println("\nECDH failed due to timeout.");
        ECDH_exchange_pending_id = 0;
        ECDH_RETRY = 0;
      }
    } else if (ECDH_exchange_pending_id == 0) {
      ECDH_RETRY = 0;
      if (AES_friendliness_pending_id > 0) {
        JsonDocument AES_Message;
        JsonDocument clavis_obfuscata;
        String clavis = encrypt(key);
        deserializeJson(clavis_obfuscata, clavis);
        AES_Message["Intention"] = "AES_Friendliness_test";
        AES_Message["AES_Key"]["IV"] = clavis_obfuscata["IV"];
        AES_Message["AES_Key"]["Payload"] = clavis_obfuscata["Payload"];
        String aes_msg;
        serializeJson(AES_Message, aes_msg);
        mesh.sendSingle(AES_friendliness_pending_id, aes_msg);
        AES_RETRY++;
        if (AES_RETRY > 5) {
          Serial.println("\nAES friendliness verification failed due to timeout.");
          AES_friendliness_pending_id = 0;
          AES_RETRY = 0;
        }
      } else if (AES_friendliness_pending_id == 0) AES_RETRY = 0;
    }
  }
}
void setup() {
  Serial.begin(115200);
  /*
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  disableCore0WDT();
  disableCore1WDT();
  disableLoopWDT();
  esp_task_wdt_delete(NULL);// kill the dogs*/
  uECC_set_rng(&RNG);
  Serial.println("Hieroglossa Development Project v.0.1.3");
  Serial.println("Configuring Mesh");
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&received_JSON_parser);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  // Create task for FreeRTOS
  Serial.println("Configuring FreeRTOS");
  xTaskCreatePinnedToCore(RTOS_mesh_update,  // Task function
                          "Feed_Mesh",       // Task name
                          4096,              // Stack size
                          NULL,
                          1,                           // Priority
                          NULL,                        // handle
                          CONFIG_ARDUINO_RUNNING_CORE  // Core 1
  );                                                   // TaskHandle
  xTaskCreate(RTOS_feed_watchdog_timer,                // Task function
              "Feed_WDT",                              // Task name
              1024,                                    // Stack size
              NULL,
              3,    // Priority
              NULL  // handle
  );
  xTaskCreate(RTOS_ECDH_handler,  // Task function
              "ECDH",             // Task name
              65536,              // Stack size
              NULL,
              2,    // Priority
              NULL  // handle
  );
  Serial.println("Finished Setup.");
}

void loop() {
}
