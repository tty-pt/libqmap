# libqmap
> A small library for in-memory maps with optional persistence and a CLI tool.

**⚠️ Important:** libqmap uses global state and is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

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
File-backed maps are **automatically saved at process exit**.

**Note on File Persistence:**
- File loading happens automatically when opening a file-backed map (no flags required)
- The `QM_MIRROR` flag enables automatic reverse-lookup (bidirectional maps)
- When using `QM_MIRROR`, closing the primary map automatically closes the mirror (handle + 1)

Persistent example:
```c
// Simple file persistence (read/write without mirroring)
uint32_t hd = qmap_open("data.qmap", "main", QM_U32, QM_STR, 0xFF, 0);
qmap_put(hd, &key, value);
qmap_save();  // Optional explicit save
qmap_close(hd);

// File persistence with bidirectional mirroring
uint32_t hd = qmap_open("data.qmap", "main", QM_U32, QM_STR, 0xFF, QM_MIRROR);
qmap_put(hd, &key, value);
qmap_save();  // Optional explicit save
qmap_close(hd);  // Mirror map (hd + 1) is automatically closed
```

## Docs
Use the man pages for complete library and CLI documentation:
```sh
man qmap_open
man qmap
```

If you prefer reading code, the CLI entry point is `src/qmap.c`.
