# distributed-audio-processing

## Test Server with dummy client

### 1. Run server

On terminal run:

```bash
. ./.env
make run-server
```

### 2. Run dummy client

On a different terminal run:

```bash
gcc -Wall -Wextra -std=gnu99 -Iinclude dummy_client.c build/lib/libcrypto.a build/lib/libcommon.a -o bin/dummy_client 
. ./.env
./bin/dummy_client
```
