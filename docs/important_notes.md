# Important Notes

### Important Notes for the Encryptor.cpp implementation:

- GCM Context Management: The mbedtls_gcm_context should ideally be re-initialized (mbedtls_gcm_init) or reset (mbedtls_gcm_setkey effectively does this for the key part) if you are using the same context for multiple independent encryption/decryption operations with different keys or modes. For simplicity here, I've added a mbedtls_gcm_free and mbedtls_gcm_init after each operation. A more performant approach for multiple operations with the same key would be to call mbedtls_gcm_setkey once, then mbedtls_gcm_starts (or mbedtls_gcm_crypt_and_tag / mbedtls_gcm_auth_decrypt which handle starts internally for single-shot ops) multiple times with different IVs. Since our encrypt and decrypt methods are self-contained and take the key each time, re-initializing or relying on setkey to reset is fine.

- RNG Personalization: The personalization string for mbedtls_ctr_drbg_seed is important. Using a fixed string like "SecureStorageEncryptorSeed" is okay, but for enhanced security in some scenarios, appending device-unique data (like part of the serial number, if not sensitive itself) can be beneficial.

- Error Handling in Encryptor::Impl Constructor: If mbedtls_ctr_drbg_seed fails, the Encryptor is left in a bad state. The current code logs this. In a production system, this might warrant throwing an exception during construction or having an isValid() method that the caller must check. My Error::Errc Encryptor::encrypt/decrypt methods now check m_impl->initialized.