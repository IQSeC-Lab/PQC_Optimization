// Client that performs Kyber-1024 key exchange and encrypts a message with AES-GCM
// Compile with:
// gcc mbedtls_Client.c -o client -lmbedtls -lmbedx509 -lmbedcrypto -loqs -lssl -lcrypto -L/usr/local/lib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <oqs/oqs.h>
#include <time.h>
#include <mbedtls/gcm.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#define SERVER_IP "192.168.1.101"  
#define PORT 8080
#define BUFFER_SIZE 2048
#define AES_KEY_SIZE 32
#define AES_IV_SIZE 12
#define AES_TAG_SIZE 16

int send_encrypted_message(int client_socket, uint8_t *iv, uint8_t *ciphertext, size_t cipher_len, uint8_t *tag) {
    if (send(client_socket, iv, AES_IV_SIZE, 0) != AES_IV_SIZE) return -1;
    if (send(client_socket, tag, AES_TAG_SIZE, 0) != AES_TAG_SIZE) return -1;
    if (send(client_socket, ciphertext, cipher_len, 0) != (int)cipher_len) return -1;
    return 0;
}

int aes_gcm_encrypt(const unsigned char *plaintext, size_t len, const unsigned char *aes_key, unsigned char *iv, unsigned char *ciphertext, unsigned char *tag) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, AES_KEY_SIZE * 8) != 0) {
        printf("[ERROR] Failed to set AES-GCM key!\n");
        mbedtls_gcm_free(&gcm);
        return -1;
    }

    if (RAND_bytes(iv, AES_IV_SIZE) != 1) {
        printf("[ERROR] Failed to generate random IV!\n");
        mbedtls_gcm_free(&gcm);
        return -1;
    }

    int ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, iv, AES_IV_SIZE, NULL, 0, plaintext, ciphertext, AES_TAG_SIZE, tag);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        printf("[CLIENT] AES-GCM encryption failed!\n");
        return -1;
    }

    return (int)len;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <FILENAME>\n", argv[0]);
        exit(1);
    }

    const char *filename = argv[1];

    // const char *kem_alg = OQS_KEM_alg_bike_l1;
    // const char *kem_alg = OQS_KEM_alg_bike_l3;
    // const char *kem_alg = OQS_KEM_alg_bike_l5;
    // const char *kem_alg = OQS_KEM_alg_hqc_128;
    // const char *kem_alg = OQS_KEM_alg_hqc_192;
    // const char *kem_alg = OQS_KEM_alg_hqc_256;
    // const char *kem_alg = OQS_KEM_alg_kyber_512;
    // const char *kem_alg = OQS_KEM_alg_kyber_768;
    const char *kem_alg = OQS_KEM_alg_kyber_1024;


    

    // const char *kem_alg = OQS_KEM_alg_kyber_1024;// Here change the algorithm

    int client_socket;
    struct sockaddr_in server_addr;
    uint8_t iv[AES_IV_SIZE];

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("[ERROR] Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] Connection failed");
        exit(1);
    }

    printf("[CLIENT] Connected to server. Performing key exchange using %s...\n", kem_alg);

    OQS_KEM *kem = OQS_KEM_new(kem_alg);
    if (!kem) {
        printf("[ERROR] Failed to initialize KEM for algorithm %s\n", kem_alg);
        exit(1);
    }

    uint8_t *public_key = malloc(kem->length_public_key);
    uint8_t *ciphertext = malloc(kem->length_ciphertext);
    uint8_t *shared_secret = malloc(kem->length_shared_secret);

    if (!public_key || !ciphertext || !shared_secret) {
        printf("[ERROR] Memory allocation failed!\n");
        goto cleanup;
    }

    if (recv(client_socket, public_key, kem->length_public_key, 0) != (int)kem->length_public_key) {
        printf("[ERROR] Failed to receive public key from server!\n");
        goto cleanup;
    }

    if (OQS_KEM_encaps(kem, ciphertext, shared_secret, public_key) != OQS_SUCCESS) {
        printf("[ERROR] KEM encapsulation failed!\n");
        goto cleanup;
    }

    if (send(client_socket, ciphertext, kem->length_ciphertext, 0) != (int)kem->length_ciphertext) {
        printf("[ERROR] Failed to send ciphertext to server!\n");
        goto cleanup;
    }

    printf("[CLIENT] Key exchange complete. Shared secret established.\n");

    // Derive AES key from shared secret
    uint8_t aes_key[AES_KEY_SIZE];
    SHA256(shared_secret, kem->length_shared_secret, aes_key);

    // Read message from file
    char message[BUFFER_SIZE] = {0};
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("[ERROR] Failed to open input file");
        goto cleanup;
    }
    fread(message, 1, BUFFER_SIZE - 1, fp);
    fclose(fp);

    uint8_t encrypted_msg[BUFFER_SIZE];
    uint8_t tag[AES_TAG_SIZE];
    int encrypted_len = aes_gcm_encrypt((unsigned char*)message, strlen(message), aes_key, iv, encrypted_msg, tag);

    if (encrypted_len < 0) {
        goto cleanup;
    }

    if (send_encrypted_message(client_socket, iv, encrypted_msg, encrypted_len, tag) < 0) {
        perror("[CLIENT] Failed to send encrypted message");
        goto cleanup;
    }

    printf("[CLIENT] Encrypted message sent successfully.\n");
    printf("[CLIENT] AES-GCM Tag: ");
    for (int i = 0; i < AES_TAG_SIZE; i++) {
        printf("%02X ", tag[i]);
    }
    printf("\n");

cleanup:
    OQS_MEM_secure_free(public_key, kem->length_public_key);
    OQS_MEM_secure_free(ciphertext, kem->length_ciphertext);
    OQS_MEM_secure_free(shared_secret, kem->length_shared_secret);
    OQS_KEM_free(kem);
    close(client_socket);
    return 0;
}
