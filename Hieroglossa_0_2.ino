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
AESTiny128 aes128;

// Define a key for encryption
String key = "0123456789ABCDEF0123456789ABCDEF";

// Define a JSON document to store AES keys of every node connected
JsonDocument keyring;

// Variable to store time elapsed
unsigned long time_elapsed;

// Variable to store the result of encryption
String encryption_result_str;

// Function to randomize the initialization vector (IV)
String randomize_iv(uint8_t *iv) {
  // Seed the random number generator with an analog reading from pin 35
  randomSeed(analogRead(35));

  // Initialize a string to store the hexadecimal representation of the IV
  String init_vector_str;

  // Generate random bytes to populate the IV
  for (int i = 0; i < 16; i++) {
    iv[i] = random(256);  // Generate a random byte (0-255) and store it in the IV
  }

  // Convert the IV to a hexadecimal string
  init_vector_str = ByteArrayToStringHex(iv, 16);

  // Print the shuffled initialization vector
  Serial.println("Initialization Vector shuffled: " + init_vector_str);

  // Return the hexadecimal string representation of the IV
  return init_vector_str;
}

// Function to pad plaintext using PKCS#7 padding scheme
String pkcs7pad(String plaintext) {
  // Calculate the number of bytes needed for padding
  int paddingLength = 16 - (plaintext.length() % 16);

  // Add padding bytes to the plaintext
  for (int i = 0; i < paddingLength; ++i) {
    plaintext += (char)paddingLength;  // Append a byte with the value equal to the padding length
  }

  // Return the padded plaintext
  return plaintext;
}

// Function to unpad plaintext using PKCS#7 padding scheme
String pkcs7unpad(String paddedText) {
  // Retrieve the padding length from the last byte of the padded text
  int paddingLength = paddedText[paddedText.length() - 1];

  // Remove the padding from the plaintext
  return paddedText.substring(0, paddedText.length() - paddingLength);  // Remove the last n bytes, where n is the padding length
}

// Structure to hold the cipher (encrypted data and IV)
struct cipher {
  String hx;  // Hexadecimal representation of the encrypted data
  String iv;  // Initialization Vector used for encryption
};

// Function prototype for en/decryption
cipher encrypt(String raw);
String decrypt(cipher cf);
// Function to perform encryption
cipher encrypt(String raw) {
  String ciphertext = "";                // Variable to store the ciphertext
  String result = "";                    // Unused variable
  uint8_t buffer[16];                    // Buffer to hold data blocks for encryption
  unsigned int retry = 0;                // Number of retry attempts for encryption
  uint8_t iv[16] = {};                   // Initialization Vector (IV) for encryption (initialized to all zeros)
  String iv_encrypt = randomize_iv(iv);  // Generate a random IV and convert it to a string
  time_elapsed = micros();               // Record the current time for performance measurement
  raw.trim();                            // Trim any leading/trailing whitespace from the plaintext
  Serial.println(raw);                   // Print the plaintext (for debugging purposes)

  // PKCS7 Padding
  bool encryption_error = 0;  // Flag to indicate if an encryption error occurred

  // Encryption
  do {
    String content = pkcs7pad(raw);  // Pad the plaintext using PKCS#7 padding scheme
    ciphertext = "";                 // Clear the ciphertext string
    result = "";                     // Clear the result string

    // Encrypt each block of the padded plaintext
    while (content.length() > 0) {
      String plaintext = content.substring(0, 16);  // Extract the next 16-byte block of plaintext
      content = content.substring(16);              // Remove the processed portion of plaintext from the content

      // Set the AES encryption key
      aes128.setKey(reinterpret_cast<const uint8_t *>(key.c_str()), 16);  // Convert the key string to a byte array and set it as the AES key

      // XOR the plaintext block with the IV
      for (int i = 0; i < 16; ++i) {
        buffer[i] = plaintext[i] ^ iv[i];  // Compute the XOR of the plaintext and IV and store it in the buffer
      }

      // Encrypt the XORed data block
      aes128.encryptBlock(buffer, buffer);  // Perform AES encryption on the buffer

      // XOR the encrypted data block with the IV to produce the ciphertext block
      for (int i = 0; i < 16; ++i) {
        buffer[i] ^= iv[i];                     // Compute the XOR of the encrypted data block and IV and store it in the buffer
        ciphertext += String((char)buffer[i]);  // Append each byte of the ciphertext block to the ciphertext string
      }
    }

    // Check if the length of the ciphertext is a multiple of 16 bytes
    if (ciphertext.length() % 16 != 0) {
      encryption_error = 1;                                                  // Set encryption error flag
      Serial.println("Error: Ciphertext length mismatch detected, retry.");  // Print error message

      // Generate a new random IV for retry
      iv_encrypt = randomize_iv(iv);  // Generate a new random IV and convert it to a string

      retry++;                                 // Increment the retry count
      if (retry > 50) {                        // Check if maximum retry attempts reached
        Serial.println("Encryption failed.");  // Print failure message if maximum retries reached
      }
    } else {
      encryption_error = 0;  // Clear encryption error flag
    }
  } while (encryption_error == 1);  // Repeat encryption process until no error occurs

  // End of Encryption

  // Print plaintext and ciphertext lengths and encryption time
  Serial.println("Plaintext Length: " + String(raw.length()));
  Serial.println("Ciphertext Length: " + String(ciphertext.length()));
  time_elapsed = micros() - time_elapsed;                          // Calculate encryption time
  Serial.println("Encryption time /us: " + String(time_elapsed));  // Print encryption time

  // Convert ciphertext to hexadecimal string
  String hexString = StringRawToStringHex(ciphertext);  // Convert the raw ciphertext to a hexadecimal string

  // Print hexadecimal ciphertext
  Serial.println(hexString);

  // Create a cipher object to store the IV and ciphertext
  cipher rtr;
  rtr.iv = iv_encrypt;  // Store the IV in the cipher object
  rtr.hx = hexString;   // Store the hexadecimal ciphertext in the cipher object

  return rtr;  // Return the cipher object containing IV and ciphertext
}
// Function to decrypt ciphertext using AES algorithm
String decrypt(cipher cf) {
  String iv_decrypt = cf.iv;  // Extract IV from cipher object
  String hexString = cf.hx;   // Extract ciphertext from cipher object
  uint8_t buffer[16];         // Buffer to hold intermediate data during decryption
  String cipher = "";         // Initialize string to hold raw ciphertext
  String result = "";         // Initialize string to hold decrypted plaintext
  time_elapsed = micros();    // Record the current time for decryption performance measurement

  byte iv[16];                                            // Initialize array to hold IV bytes
  hexStringToByteArray(iv_decrypt, iv, 16);               // Convert IV from hexadecimal string to byte array
  Serial.println("IV: " + ByteArrayToStringHex(iv, 16));  // Print IV in hexadecimal format
  Serial.println("Cipher: " + hexString);                 // Print ciphertext

  // Convert hexadecimal ciphertext to raw string
  for (int i = 0; i < hexString.length(); i += 2) {
    String hexByte = hexString.substring(i, i + 2);  // Extract two characters representing a byte
    char c = strtol(hexByte.c_str(), NULL, 16);      // Convert hexadecimal string to byte
    cipher += c;                                     // Append byte to raw ciphertext string
  }

  Serial.println("Ciphertext Length: " + String(cipher.length()));  // Print length of raw ciphertext

  // Decryption process
  while (cipher.length() > 0) {
    String plaintext = cipher.substring(0, 16);  // Extract a block of ciphertext (16 bytes)
    cipher = cipher.substring(16);               // Remove the processed ciphertext block

    // Set AES key and perform decryption
    aes128.setKey(reinterpret_cast<const uint8_t *>(key.c_str()), 16);  // Set AES key
    for (int i = 0; i < 16; ++i) {
      buffer[i] = plaintext[i] ^ iv[i];  // XOR ciphertext with IV
    }
    aes128.decryptBlock(buffer, buffer);  // Perform AES decryption
    for (int i = 0; i < 16; ++i) {
      buffer[i] ^= iv[i];                 // XOR the decrypted block with IV
      result += String((char)buffer[i]);  // Append decrypted byte to result string
    }
  }

  // Remove PKCS#7 padding from plaintext
  result = pkcs7unpad(result);

  // Print decryption metrics
  time_elapsed = micros() - time_elapsed;                          // Calculate decryption time
  Serial.println("Decryption result: " + result);                  // Print decrypted plaintext
  Serial.println("Decryption time /us: " + String(time_elapsed));  // Print decryption time

  return result;  // Return decrypted plaintext
}


// Mesh
#include "painlessMesh.h"
#define MESH_PREFIX "Hieroglossa"
#define MESH_PASSWORD "Hieroglossa"
#define MESH_PORT 5555
painlessMesh mesh;
Scheduler userScheduler;

// Define the ECDH parameters
const struct uECC_Curve_t *curve = uECC_secp160r1();  // Using secp160r1 curve
uint8_t private_key[21];                              // Private key for ECDH (160 bits + 1 for padding)
uint8_t public_key[40];                               // Public key for ECDH (2 * 160 bits)
uint8_t recipient_public_key[40];                     // Public key of the recipient for ECDH
uint8_t secret[20];                                   // Shared secret after ECDH (160 bits)

// Define a class for mesh objects
class mesh_object {
public:
  JsonDocument doc;                 // JSON document to store message data
  String str;                       // String representation of the JSON message
  uint32_t ID = 0;                  // ID of the message
  bool ACK_REQ_from_recipient = 1;  // Whether acknowledgment is required from the recipient

  // Constructor
  mesh_object() {
    // Optionally initialize members here
  }

  // Destructor
  ~mesh_object() {
    // Optionally clean up resources here
  }

  // Method to deserialize JSON string into JSON document
  void deser() {
    deserializeJson(doc, str);
  }

  // Method to serialize JSON document into JSON string
  String ser() {
    // Populate JSON document with necessary fields
    doc["Sender"] = String(mesh.getNodeId());            // Sender ID
    doc["Recipient"] = String(ID);                       // Recipient ID
    doc["Transaction_ID"] = String(mesh.getNodeTime());  // Transaction ID
    doc["ACK_REQ"] = ACK_REQ_from_recipient;             // Whether acknowledgment is required
    // Serialize JSON document into JSON string
    serializeJson(doc, str);
    return str;  // Return the serialized JSON string
  }
};

mesh_object inbound;   // Object to handle inbound messages
mesh_object outbound;  // Object to handle outbound messages


uint32_t ECDH_exchange_pending_id = 0;
unsigned int AES_friendliness_pending_id = 0;

/* Random Number Generator */

// Function to generate random numbers
// Parameters:
//   - dest: Pointer to the destination buffer to store random numbers
//   - size: Number of random bytes to generate
// Returns:
//   - 1 on success
static int RNG(uint8_t *dest, unsigned size) {
  // Loop until the desired number of random bytes is generated
  while (size) {
    uint8_t val = 0;  // Initialize the variable to store the random byte
    // Generate each random byte (8 bits)
    for (unsigned i = 0; i < 8; ++i) {
      int init = analogRead(35);  // Read an analog value from pin 35 (may need to be adjusted based on your hardware setup)
      int count = 0;              // Initialize a counter to track the number of consecutive readings
      // Generate random fluctuations and count how many times the analog reading changes
      while (analogRead(35) + random(-100, 100) == init) ++count;
      // If no change is detected, append the least significant bit of the initial reading to the value
      if (count == 0) val = (val << 1) | (init & 0x01);
      // If changes are detected, append the least significant bit of the count to the value
      else val = (val << 1) | (count & 0x01);
    }
    // Store the generated random byte in the destination buffer
    *dest = val;
    ++dest;
    --size;  // Decrement the remaining size of random bytes to generate
  }
  return 1;  // Return 1 to indicate successful generation of random numbers
}

/* Outbound Message Queue */
// Define a structure to represent a message
struct message {
  unsigned long id;           // Unique ID of the message
  uint32_t recipient;         // Recipient ID of the message
  String content;             // Content of the message
  unsigned int sendAttempts;  // Number of send attempts for the message
};

// Define a vector to store messages
std::vector<message> messageQueue;

// Function to add a message to the message queue
void add_to_message_queue(uint32_t recipient, uint32_t id, String content) {
  Serial.println(content);
  // Create a new message instance
  message newMessage;
  // Set the attributes of the message
  newMessage.id = id;
  newMessage.recipient = recipient;
  newMessage.content = content;
  // Push the new message to the message queue
  messageQueue.push_back(newMessage);
  // Print the number of messages in the queue
  Serial.print("Message Queued. Total Messages: ");
  Serial.println(messageQueue.size());
}

// Function to erase a message from the message queue by its ID
void erase_from_message_queue(unsigned long id) {
  // Iterate through the message queue
  for (auto it = messageQueue.begin(); it != messageQueue.end(); ++it) {
    // Check if the ID of the current message matches the given ID
    if (it->id == id) {
      // If a match is found, erase the message from the queue
      messageQueue.erase(it);
      // Exit the loop after erasing the message
      break;
    }
  }
}


/* Inbound Message Parser */
void receivedMessageCallback(uint32_t sender, String &received_JSON_message) {
  // Print the received raw message
  Serial.println("\nReceived Raw: " + received_JSON_message);
  // Set the received JSON message to the inbound message object
  inbound.str = received_JSON_message;
  // Deserialize the JSON message
  inbound.deser();
  // Extract ACK_REQ and Recipient from the received message
  String ackRequiredStr = inbound.doc["ACK_REQ"];
  String RecipientStr = inbound.doc["Recipient"];

  // Check if the message is intended for the current node
  if (RecipientStr == String(mesh.getNodeId())) {
    // Check if acknowledgment is required
    if (ackRequiredStr == "true") {
      // Check the intention of the message
      if (inbound.doc["Intention"] == "ECDHXCG") {
        // Check if an ECDH exchange is pending with the sender
        if (ECDH_exchange_pending_id == sender) {
          // Process the ECDH exchange message
          Serial.println("\nProcessing Message.");
          // Extract the recipient's public key from the message
          String recipient_public_key_str = inbound.doc["ECDH_Key"];
          Serial.println(recipient_public_key_str);
          // Convert the recipient's public key from hexadecimal string to byte array
          hexStringToByteArray(recipient_public_key_str, recipient_public_key, recipient_public_key_str.length());
          // Perform ECDH key exchange
          Serial.println("Finishing ECDH Exchange.");
          if (!uECC_shared_secret(recipient_public_key, private_key, secret, curve)) {
            Serial.println("Error: failed computing a shared AES key, retry.");
          } else {
            // Convert the shared secret to hexadecimal string
            key = ByteArrayToStringHex(secret, sizeof(secret));
            // Print the acquired AES key
            Serial.println("\nNode ID " + String(sender) + ": AES key acquired via ECDH: " + key);
            // Save the AES key to the keyring for the sender
            keyring[String(sender)] = key;
            // Reset the ECDH exchange pending ID
            ECDH_exchange_pending_id = 0;
            // Set the AES friendliness pending ID to the sender
            AES_friendliness_pending_id = sender;
            // Encrypt the acknowledgment message using the acquired AES key
            cipher cf = encrypt(key);
            // Create an AES handshake message
            mesh_object aes_handshake;
            aes_handshake.doc["Intention"] = "ECDHACK";
            aes_handshake.ID = sender;
            aes_handshake.doc["IV"] = cf.iv;
            aes_handshake.doc["Payload"] = cf.hx;
            aes_handshake.ser();  // Write to outbound string
            // Add the AES handshake message to the message queue
            add_to_message_queue(aes_handshake.ID, millis(), aes_handshake.str);
          }
        }
      } else if (inbound.doc["Intention"] == "ECDHACK") {
        // If the intention is to acknowledge the ECDH exchange
        // Decrypt the acknowledgment message using the shared AES key
        cipher cf;
        cf.iv = String(inbound.doc["IV"].as<const char *>());
        cf.hx = String(inbound.doc["Payload"].as<const char *>());
        // Check if the decrypted message matches the shared AES key
        if (decrypt(cf) == key) {
          // If the acknowledgment is valid
          Serial.println("ECDH Exchange Successful.");
          // Save the AES key to the keyring for the sender
          String key_id = String(AES_friendliness_pending_id);
          keyring[key_id] = key;
          Serial.println("Secure channel established with Node " + key_id);
          Serial.println("Key " + key + " saved to keyring for Node " + key_id);
          // Reset the ECDH exchange pending ID
          ECDH_exchange_pending_id = 0;
          // Reset the AES friendliness pending ID
          AES_friendliness_pending_id = 0;
        } else {
          // If the acknowledgment is invalid
          Serial.println("Not a match.");
        }
      } else {
        // If the intention is unknown
        Serial.println("Message ignored.");
      }
    }
  }
}

void ECDH_NewConnectionCallback(uint32_t nodeId) {
  // Set the outbound ID to the provided node ID
  outbound.ID = nodeId;
  // Print a message indicating connection with the node
  Serial.println("Connected with node: " + String(outbound.ID));
  // Retrieve saved credentials for the node
  String savedCredentials = keyring[String(outbound.ID)];
  // Print the saved credentials
  Serial.println("Saved Credentials: " + savedCredentials);
  // Check if saved credentials exist
  if (savedCredentials != "null") {
    // If saved credentials exist, the node is a friend
    Serial.println("Node is a friend.");
    // Set the pending ECDH exchange ID to the outbound ID
    ECDH_exchange_pending_id = outbound.ID;
  } else {
    // If saved credentials don't exist, the node is a stranger
    Serial.println("Node is a stranger.");
    // Generate ECC key pair
    Serial.println("Generating ECC Key.");
    uECC_make_key(public_key, private_key, curve);
    // Print the generated ECC key pair
    Serial.println("Generated ECC key pair: \nECC Public: " + ByteArrayToStringHex(public_key, sizeof(public_key)) + "\nECC Private: " + ByteArrayToStringHex(private_key, sizeof(private_key)));
    Serial.println();
    // Set the pending ECDH exchange ID to the outbound ID
    ECDH_exchange_pending_id = outbound.ID;
    // Initiate ECDH Exchange
    Serial.println("Initiate ECDH Exchange.");
    String ECDH_hex = ByteArrayToStringHex(public_key, sizeof(public_key));
    // Populate outbound message with ECDH parameters
    outbound.doc["Intention"] = "ECDHXCG";
    outbound.doc["ECDH_Key"] = ECDH_hex;
    outbound.doc["ACK_REQ"] = true;
    Serial.println(outbound.ser());
    // Add the outbound message to the message queue
    add_to_message_queue(outbound.ID, millis(), outbound.str);
  }
}


void changedConnectionCallback() {
  Serial.print("\nConnection Changed.");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  //Serial.println("Timestamp adjusted to: " + String(mesh.getNodeTime()));
}

void monitorTaskStackUsage(TaskHandle_t taskHandle) {
  const uint32_t stackSize = uxTaskGetStackHighWaterMark(taskHandle);
  Serial.print("Task stack usage: ");
  Serial.print(stackSize);
  Serial.println(" bytes remaining.");
}

void setup() {
  Serial.begin(115200);
  uECC_set_rng(&RNG);
  Serial.println("Hieroglossa Development Project v.0.2.0");
  Serial.println("Configuring Mesh");
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedMessageCallback);
  mesh.onNewConnection(&ECDH_NewConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
  Serial.println("Node ID: " + String(mesh.getNodeId()));
  Serial.println("Configuring FreeRTOS");
  //xTaskCreatePinnedToCore(RTOS_mesh_send_queue, "Feed_Mesh", 131072, NULL, 1, &RTOS_mesh_send_queue_handle, CONFIG_ARDUINO_RUNNING_CORE);
  //xTaskCreatePinnedToCore(RTOS_iterate_message_queue, "Send_Message_Queue", 131072, NULL, 2, &RTOS_iterate_message_queue_handle, CONFIG_ARDUINO_RUNNING_CORE);
  Serial.println("Finished Setup.");
}

void loop() {
  esp_task_wdt_feed();
  mesh.update();
  // Check if the message queue is not empty
  if (!messageQueue.empty()) {
    Serial.print("Pinging");  // Print a dot to indicate that messages were sent
    // Iterate through each message in the queue
    for (size_t i = 0; i < messageQueue.size(); ++i) {
      // Send the message using mesh.sendSingle
      mesh.sendSingle(messageQueue[i].recipient, messageQueue[i].content);
      Serial.print(")");
      // Increment the sendAttempts counter for this message
      messageQueue[i].sendAttempts++;
      // If the message has been sent without acknowledgment, erase it
      if (messageQueue[i].sendAttempts >= 10) {
        messageQueue.erase(messageQueue.begin() + i);
        // Decrement the loop counter as the size of messageQueue has changed
        i--;
      }
      // Delay before sending the next message (Overflow control)
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}
