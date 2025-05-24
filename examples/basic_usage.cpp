#include "SecureStorageManager.h"       // For Error::Errc, even if not using SecureStorageManager directly
#include "crypto/KeyProvider.h"
#include "crypto/Encryptor.h"
#include "utils/FileUtil.h"
#include "utils/Logger.h"        // For SS_LOG macros

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>      // For std::hex, std::setw, std::setfill
#include <algorithm>    // For std::all_of, std::isxdigit

// Helper to print byte vector as hex (from previous example)
void printHex(const std::string& prefix, const std::vector<unsigned char>& data) {
    std::cout << prefix;
    if (data.empty()) {
        std::cout << "<empty>";
    } else {
        std::cout << "0x";
        for (unsigned char byte : data) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
    }
    std::cout << std::dec << std::endl;
}

void printUsage() {
    std::cerr << "SecureStorage File Utility" << std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  SecureStorageExample encrypt <serial_number> <input_plaintext_file> <output_encrypted_file>" << std::endl;
    std::cerr << "  SecureStorageExample decrypt <serial_number> <input_encrypted_file> <output_plaintext_file>" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Arguments:" << std::endl;
    std::cerr << "  encrypt/decrypt       : Operation mode." << std::endl;
    std::cerr << "  <serial_number>       : Device serial number (e.g., 9 digits) for key derivation." << std::endl;
    std::cerr << "  <input_file>          : Path to the input file." << std::endl;
    std::cerr << "  <output_file>         : Path to the output file." << std::endl;
}

int main(int argc, char* argv[]) {
    // Configure logger (optional, sensible defaults are usually fine)
    // SecureStorage::Utils::Logger::getInstance().setLogLevel(SecureStorage::Utils::LogLevel::DEBUG);

    std::cout << "Argument Count: "<< argc << std::endl;
    if (argc < 5) {
        printUsage();
        return 1;
    }

    std::string mode = argv[1];
    std::string serialNumber = argv[2];
    std::string inputFile = argv[3];
    std::string outputFile = argv[4];

    if (serialNumber.empty()) {
        SS_LOG_ERROR("Serial number cannot be empty.");
        printUsage();
        return 1;
    }
    if (inputFile.empty() || outputFile.empty()) {
        SS_LOG_ERROR("Input and output file paths cannot be empty.");
        printUsage();
        return 1;
    }

    SS_LOG_INFO("Mode: " << mode);
    SS_LOG_INFO("Serial Number: " << serialNumber);
    SS_LOG_INFO("Input File: " << inputFile);
    SS_LOG_INFO("Output File: " << outputFile);

    // Initialize KeyProvider and Encryptor directly
    // For C++11, use new for unique_ptr
    std::unique_ptr<SecureStorage::Crypto::KeyProvider> keyProvider(
        new SecureStorage::Crypto::KeyProvider(serialNumber)
    );
    // Default personalization string for Encryptor is usually fine for this kind of tool
    std::unique_ptr<SecureStorage::Crypto::Encryptor> encryptor(
        new SecureStorage::Crypto::Encryptor()
    );

    std::vector<unsigned char> masterKey;
    SecureStorage::Error::Errc keyErr = keyProvider->getEncryptionKey(masterKey, SecureStorage::Crypto::AES_GCM_KEY_SIZE_BYTES);
    if (keyErr != SecureStorage::Error::Errc::Success) {
        SS_LOG_ERROR("Failed to derive master key. Error: " 
            << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(keyErr)));
        return 1;
    }
    SS_LOG_DEBUG("Master key derived successfully.");

    if (mode == "encrypt") {
        SS_LOG_INFO("Starting encryption process...");
        std::vector<unsigned char> plaintextData;
        SecureStorage::Error::Errc readErr = SecureStorage::Utils::FileUtil::readFile(inputFile, plaintextData);
        if (readErr != SecureStorage::Error::Errc::Success) {
            SS_LOG_ERROR("Failed to read plaintext file '" << inputFile << "'. Error: "
                << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(readErr)));
            return 1;
        }
        if (plaintextData.empty() && !SecureStorage::Utils::FileUtil::pathExists(inputFile)) {
             SS_LOG_ERROR("Input plaintext file '" << inputFile << "' does not exist or is empty and could not be read as empty.");
             return 1;
        }
         if (plaintextData.empty() && SecureStorage::Utils::FileUtil::pathExists(inputFile)) {
            SS_LOG_INFO("Input plaintext file '" << inputFile << "' is empty. Proceeding with empty plaintext encryption.");
        }


        std::vector<unsigned char> encryptedData;
        SecureStorage::Error::Errc encErr = encryptor->encrypt(plaintextData, masterKey, encryptedData);
        if (encErr != SecureStorage::Error::Errc::Success) {
            SS_LOG_ERROR("Encryption failed. Error: " 
                << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(encErr)));
            return 1;
        }

        SecureStorage::Error::Errc writeErr = SecureStorage::Utils::FileUtil::atomicWriteFile(outputFile, encryptedData);
        if (writeErr != SecureStorage::Error::Errc::Success) {
            SS_LOG_ERROR("Failed to write encrypted data to '" << outputFile << "'. Error: "
                << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(writeErr)));
            return 1;
        }
        SS_LOG_INFO("Successfully encrypted '" << inputFile << "' to '" << outputFile << "'.");
        printHex("Encrypted data sample (first 16 bytes if long): ", 
            std::vector<unsigned char>(encryptedData.begin(), encryptedData.begin() + std::min(static_cast<size_t>(16), encryptedData.size())));

    } else if (mode == "decrypt") {
        SS_LOG_INFO("Starting decryption process...");
        std::vector<unsigned char> encryptedData;
        SecureStorage::Error::Errc readErr = SecureStorage::Utils::FileUtil::readFile(inputFile, encryptedData);
        if (readErr != SecureStorage::Error::Errc::Success) {
            SS_LOG_ERROR("Failed to read encrypted file '" << inputFile << "'. Error: "
                << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(readErr)));
            return 1;
        }
        if (encryptedData.empty()) {
            SS_LOG_ERROR("Encrypted file '" << inputFile << "' is empty or could not be read.");
            return 1;
        }

        std::vector<unsigned char> decryptedData;
        SecureStorage::Error::Errc decErr = encryptor->decrypt(encryptedData, masterKey, decryptedData);
        if (decErr != SecureStorage::Error::Errc::Success) {
            SS_LOG_ERROR("Decryption failed. Error: " 
                << SecureStorage::Error::SecureStorageErrorCategory::get().message(static_cast<int>(decErr))
                << ". This could be due to a wrong serial number (key), tampered data, or if the file was not encrypted by this tool/library.");
            return 1;
        }

        // For this example, let's write to output file.
        // If outputFile is "-", print to stdout.
        if (outputFile == "-") {
            SS_LOG_INFO("Successfully decrypted data from '" << inputFile << "'. Outputting to console:");
            // If data is text, print as string. If binary, print hex or indicate binary.
            // Assuming it might be text for this example printout:
            std::string decryptedStr(decryptedData.begin(), decryptedData.end());
            std::cout << "---DECRYPTED CONTENT START---" << std::endl;
            std::cout << decryptedStr << std::endl;
            std::cout << "---DECRYPTED CONTENT END---" << std::endl;
            printHex("Decrypted data (hex): ", decryptedData);
        } else {
            std::ofstream ofs(outputFile, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                SS_LOG_ERROR("Failed to open output file '" << outputFile << "' for writing decrypted data.");
                return 1;
            }
            if (!decryptedData.empty()) {
                ofs.write(reinterpret_cast<const char*>(decryptedData.data()), decryptedData.size());
            }
            ofs.close();
            if (ofs.fail()) {
                 SS_LOG_ERROR("Failed to write decrypted data to '" << outputFile << "'.");
                 return 1;
            }
            SS_LOG_INFO("Successfully decrypted '" << inputFile << "' to '" << outputFile << "'.");
        }

    } else {
        SS_LOG_ERROR("Invalid mode: " << mode);
        printUsage();
        return 1;
    }

    return 0;
}
