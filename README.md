# distributed-audio-processing

## Test Server with dummy client

### 1. Run server

On terminal run:

```bash
make run-server
```

### 2. Run dummy client

On a different terminal run:

```bash
gcc -Wall -Wextra -std=gnu99 -Iinclude dummy_client.c build/lib/libcrypto.a build/lib/libcommon.a -o bin/dummy_client  -lm
. ./.env
./bin/dummy_client
```

### Note: Remember to always clean before running

```bash
make clean
```
