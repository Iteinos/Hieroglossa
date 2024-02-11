#include <Crypto.h>
#include <AES.h>
#include <uECC.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <soc/rtc_wdt.h>
void removeBackslashes(String &str) {
  for (int i = 0; i < str.length(); i++) {
    if (str.charAt(i) == '\\') {
      str.remove(i, 1);
      i--;
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


class mesh_object {
public:
  JsonDocument doc;
  String str;
  uint32_t ID = 0;
  mesh_object() {
    // Optionally initialize members here
  }
  ~mesh_object() {
    // Optionally clean up resources here
  }
  void deser() {
    deserializeJson(doc, str);
  }
  void ser() {
    serializeJson(doc, str);
  }
};

mesh_object inbound;
mesh_object outbound;

uint32_t ECDH_exchange_pending_id = 0;
unsigned int AES_friendliness_pending_id = 0;

static int RNG(uint8_t *dest, unsigned size) {
  while (size) {
    uint8_t val = 0;
    for (unsigned i = 0; i < 8; ++i) {
      int init = analogRead(35);
      int count = 0;
      while (analogRead(35) + random(-100, 100) == init) ++count;
      if (count == 0) val = (val << 1) | (init & 0x01);
      else val = (val << 1) | (count & 0x01);
    }
    *dest = val;
    ++dest;
    --size;
  }
  return 1;
}

TaskHandle_t ECDH_Handle;
String aes_msg;
void mesh_received_message_JSON_parser(uint32_t sender, String &received_JSON_message) {
  inbound.str = received_JSON_message;
  inbound.deser();
  Serial.println("Received Raw: " + inbound.str);
  if (inbound.doc["Intention"] == "ECDH_Exchange") {
    if (ECDH_exchange_pending_id == sender) {
      Serial.println("\nProcessing Message.");
      String recipient_public_key_str = inbound.doc["ECDH_Key"];
      Serial.println(recipient_public_key_str);
      hexStringToByteArray(recipient_public_key_str, recipient_public_key, recipient_public_key_str.length());
      Serial.println("Finishing ECDH Exchange.");
      if (!uECC_shared_secret(recipient_public_key, private_key, secret, curve)) {
        Serial.println("Error: failed computing a shared AES key, retry.");
      } else {
        key = ByteArrayToStringHex(secret, sizeof(secret));
        Serial.println("\nNode ID " + String(ECDH_exchange_pending_id) + ": AES key acquired via ECDH: " + key);
        keyring[String(ECDH_exchange_pending_id)] = key;
        ECDH_exchange_pending_id = 0;
      }
      AES_friendliness_pending_id = sender;
      JsonDocument AES_Message;
      JsonDocument clavis_obfuscata;
      String clavis = encrypt(key);
      deserializeJson(clavis_obfuscata, clavis);
      AES_Message["Intention"] = "AES_TEST";
      AES_Message["Event_ID"] = String(mesh.getNodeTime());
      AES_Message["AES_Key"]["IV"] = clavis_obfuscata["IV"];
      AES_Message["AES_Key"]["Payload"] = clavis_obfuscata["Payload"];
      serializeJson(AES_Message, aes_msg);
    } else {
    }
  } else if (inbound.doc["Intention"] == "AES_TEST" && AES_friendliness_pending_id > 0) {
    String encrypted_payload;
    JsonDocument encryption_literal;
    encryption_literal["IV"] = inbound.doc["AES_Key"]["IV"];
    encryption_literal["Payload"] = inbound.doc["AES_Key"]["Payload"];
    serializeJson(encryption_literal, encrypted_payload);
    Serial.println("Received Payload Raw: " + encrypted_payload);
    if (decrypt(encrypted_payload) == key) {
      Serial.println("ECDH Exchange Successful.");
      String key_id = String(AES_friendliness_pending_id);
      keyring[key_id] = key;
      Serial.println("Secure channel established with Node " + key_id);
      Serial.println("Key " + key + " saved to keyring for Node " + key_id);
      ECDH_exchange_pending_id = 0;
      AES_friendliness_pending_id = 0;
    } else Serial.println("Not a match.");
  } else {
    Serial.println("Message ignored.");
  }
}

void ECDH_handshake(uint32_t nodeId) {
  outbound.ID = nodeId;
  Serial.println("Connected with node: " + String(outbound.ID));
  String savedCredentials = keyring[String(outbound.ID)];
  Serial.println("Saved Credentials: " + savedCredentials);
  if (savedCredentials != "null") {
    Serial.println("Node is a friend.");
    //AES_friendliness_pending_id = nodeId;
    ECDH_exchange_pending_id = outbound.ID;
  } else {
    Serial.println("Node is a stranger.");
    Serial.println("Generating ECC Key.");
    // Generate the random public-private key pair
    uECC_make_key(public_key, private_key, curve);
    Serial.println("Generated ECC key pair: \nECC Public: " + ByteArrayToStringHex(public_key, sizeof(public_key)) + "\nECC Private: " + ByteArrayToStringHex(private_key, sizeof(private_key)));
    Serial.println();
    ECDH_exchange_pending_id = outbound.ID;
    Serial.println("Initiate ECDH Exchange.");
    String ECDH_hex = ByteArrayToStringHex(public_key, sizeof(public_key));
    outbound.doc["Intention"] = "ECDH_Exchange";
    outbound.doc["Event_ID"] = String(mesh.getNodeTime());
    outbound.doc["ECDH_Key"] = ECDH_hex;
    outbound.ser();
    Serial.println(outbound.str);
    Serial.print("Pinging stranger..");
  }
}

void changedConnectionCallback() {
  Serial.print("\nConnection Changed.");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  //Serial.println("Timestamp adjusted to: " + String(mesh.getNodeTime()));
}

void RTOS_mesh_update(void *parameters) {
  while (1) {
    mesh.update();
    esp_task_wdt_feed();
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  uECC_set_rng(&RNG);
  Serial.println("Hieroglossa Development Project v.0.1.3");
  Serial.println("Configuring Mesh");
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&mesh_received_message_JSON_parser);
  mesh.onNewConnection(&ECDH_handshake);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  Serial.println("Configuring FreeRTOS");
  xTaskCreatePinnedToCore(RTOS_mesh_update, "Feed_Mesh", 4096, NULL, 3, NULL, CONFIG_ARDUINO_RUNNING_CORE);
  //xTaskCreate(RTOS_handshake_auditor, "handshake", 65536, NULL, 1, NULL);
  Serial.println("Finished Setup.");
}

void loop() {
  vTaskDelay(600 / portTICK_PERIOD_MS);
  esp_task_wdt_feed();
  if (ECDH_exchange_pending_id > 0 || AES_friendliness_pending_id > 0) {
    mesh.sendSingle(outbound.ID, outbound.str);
    Serial.print(".");
  }
}
