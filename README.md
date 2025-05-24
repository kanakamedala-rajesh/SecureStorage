# SecureStorage

### Build Instructions

```bash
mkdir build
cd build
cmake .. 
cmake --build .
cd tests
ctest

```

### Example app usage
Run from the build output directory (e.g., build/examples/):

- To Encrypt:
```
Bash
./examples/SecureStorageExample encrypt YOUR_SERIAL_NUMBER path/to/plaintext.txt path/to/encrypted_output.enc
```
(Replace YOUR_SERIAL_NUMBER, path/to/plaintext.txt, and path/to/encrypted_output.enc with actual values.)

- To Decrypt:
```Bash
./examples/SecureStorageExample decrypt YOUR_SERIAL_NUMBER path/to/encrypted_output.enc path/to/decrypted_plaintext.txt
```

Or to print to console:
```Bash
./examples/SecureStorageExample decrypt YOUR_SERIAL_NUMBER path/to/encrypted_output.enc -
```