# libqmap
> A small library for in-memory maps with optional persistence and a CLI tool.

## Installation
Check out [these instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md#install-ttypt-packages).
And use "libqmap" as the package name.

## Quick start
Library gist:
```c
#include <ttypt/qmap.h>

uint32_t hd = qmap_open(NULL, NULL, QM_U32, QM_STR, 0xFF, 0);
uint32_t key = 1;
const char *value = "hello";
qmap_put(hd, &key, value);
const char *out = qmap_get(hd, &key);
```

CLI gist:
```sh
qmap -p 1:hello data.db:u:s
qmap -g 1 data.db:u:s
```

Persistence uses a filename and optional database name; NULL means in-memory only.

Persistent example:
```c
uint32_t hd = qmap_open("data.qmap", "main", QM_U32, QM_STR, 0xFF, 0);
```

## Docs
Use the man pages for complete library and CLI documentation:
```sh
man qmap_open
man qmap
```

If you prefer reading code, the CLI entry point is `src/qmap.c`.
