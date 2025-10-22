---
date: Qmap
section: 3
title: include/ttypt/qmap.h
---

# NAME

include/ttypt/qmap.h

# SYNOPSIS

  

`#include <stdint.h>`  
`#include <stddef.h>`  
`#include <stdlib.h>`  

## Macros

\#define **QM_MISS** ((uint32_t)-1)  

## Typedefs

typedef void **qmap_assoc_t**(const void \*\*skey, const void \*const
pkey, const void \*const value)  
Association callback type.

typedef size_t **qmap_measure_t**(const void \*data)  
Callback to measure variable-size keys.

typedef int **qmap_cmp_t**(const void \*const a, const void \*const b,
size_t len)  
Comparison callback type.

## Enumerations

enum **qmap_flags** { **QM_AINDEX** = 1, **QM_MIRROR** = 2, **QM_PGET**
= 4, **QM_SORTED** = 8 }  
QMap flags.

enum **qmap_tbi** { **QM_PTR** = 0, **QM_HNDL** = 1, **QM_STR** = 2,
**QM_U32** = 3 }  

enum **qmap_if** { **QM_RANGE** = 1 }  

## Functions

uint32_t **qmap_open** (const char \*filename, const char \*database,
uint32_t ktype, uint32_t vtype, uint32_t mask, uint32_t flags)  
Open a database.

void **qmap_save** (void)  
Write all open maps to disk.

void **qmap_close** (uint32_t hd)  
Close a map (usually automatic).

const void \* **qmap_get** (uint32_t hd, const void \*const key)  
Retrieve a value by key.

uint32_t **qmap_put** (uint32_t hd, const void \*const key, const void
\*const value)  
Insert or update a pair.

void **qmap_del** (uint32_t hd, const void \*const key)  
Delete an entry by key.

void **qmap_drop** (uint32_t hd)  
Remove all entries from a map.

void **qmap_assoc** (uint32_t hd, uint32_t link, **qmap_assoc_t** cb)  
Make an association between tables.

uint32_t **qmap_iter** (uint32_t hd, const void \*const key, uint32_t
flags)  
Start iteration.

int **qmap_next** (const void \*\*key, const void \*\*value, uint32_t
cur_id)  
Fetch next key/value.

void **qmap_fin** (uint32_t cur_id)  
End iteration early.

uint32_t **qmap_reg** (size_t len)  
Register a fixed-length type.

void **qmap_cmp_set** (uint32_t ref, **qmap_cmp_t** \*cmp)  
Assign comparison function to a type.

uint32_t **qmap_mreg** (**qmap_measure_t** \*measure)  
Register a variable-length type.

size_t **qmap_len** (uint32_t type_id, const void \*data)  
Get the byte length of an element.

# Macro Definition Documentation

## \#define QM_MISS ((uint32_t)-1)

# Typedef Documentation

## typedef void qmap_assoc_t(const void \*\*skey, const void \*const pkey, const void \*const value)

Association callback type. After association, future puts/dels on the
primary will update the secondary.

**Parameters**

> *skey* Pointer to set secondary key.  
> *pkey* Primary key.  
> *value* Primary value.

## typedef int qmap_cmp_t(const void \*const a, const void \*const b, size_t len)

Comparison callback type.

**Parameters**

> *a* First object.  
> *b* Second object.  
> *len* Length in bytes.

**Returns**

> \<0, 0, or \>0.

## typedef size_t qmap_measure_t(const void \*data)

Callback to measure variable-size keys. Keys of dynamic length need
measurement when hashing/comparing beyond pointer equality.

**Parameters**

> *data* Pointer to key.

**Returns**

> Key size in bytes.

# Enumeration Type Documentation

## enum **qmap_flags**

QMap flags.

**Enumerator**

*QM_AINDEX *  
Auto–index for NULL keys.

*QM_MIRROR *  
Create reverse-lookup (secondary) map.

*QM_PGET *  
Default to obtaining primary keys instead of values.

*QM_SORTED *  
Enable sorted index support (BTREE search).

## enum **qmap_if**

**Enumerator**

*QM_RANGE *  
Continue iteration even if key differs from the initial.

## enum **qmap_tbi**

**Enumerator**

*QM_PTR *  
Pointer (hashed).

*QM_HNDL *  
Opaque handle (no hashing).

*QM_STR *  
String contents hash and compare.

*QM_U32 *  
32-bit unsigned integer (hash and mask).

# Function Documentation

## void qmap_assoc (uint32_t hd, uint32_t link, **qmap_assoc_t*** cb)*

Make an association between tables.

**Parameters**

> *hd Secondary (index) map handle. *  
> *link Primary (source) map handle. *  
> *cb Callback to produce secondary keys. NULL → use primary value. *

## void qmap_close (uint32_t hd)

Close a map (usually automatic).

**Parameters**

> *hd Handle to close. *

## void qmap_cmp_set (uint32_t ref, **qmap_cmp_t*** \* cmp)*

Assign comparison function to a type.

**Parameters**

> *ref Type ID. *  
> *cmp Comparison callback. *

## void qmap_del (uint32_t hd, const void \*const key)

Delete an entry by key.

**Parameters**

> *hd Map handle. *  
> *key Key to delete. *

## void qmap_drop (uint32_t hd)

Remove all entries from a map.

**Parameters**

> *hd Map handle. *

## void qmap_fin (uint32_t cur_id)

End iteration early.

**Parameters**

> *cur_id Cursor handle. *

## const void \* qmap_get (uint32_t hd, const void \*const key)

Retrieve a value by key.

**Parameters**

> *hd Map handle. *  
> *key Key to look up. *

**Returns**

> Pointer to value or NULL.

## uint32_t qmap_iter (uint32_t hd, const void \*const key, uint32_t flags)

Start iteration.

**Parameters**

> *hd Map handle. *  
> *key Starting key or NULL. *  
> *flags QM_RANGE valid. *

**Returns**

> Cursor handle.

## size_t qmap_len (uint32_t type_id, const void \* data)

Get the byte length of an element.

**Parameters**

> *type_id Type ID. *  
> *data Element pointer. *

**Returns**

> Size in bytes.

## uint32_t qmap_mreg (**qmap_measure_t*** \* measure)*

Register a variable-length type.

**Parameters**

> *measure Size-measuring callback. *

**Returns**

> Type ID.

## int qmap_next (const void \*\* key, const void \*\* value, uint32_t cur_id)

Fetch next key/value.

**Parameters**

> *key Pointer to key. *  
> *value Pointer to value. *  
> *cur_id Cursor handle. *

**Returns**

> 1 if valid, 0 if done.

## uint32_t qmap_open (const char \* filename, const char \* database, uint32_t ktype, uint32_t vtype, uint32_t mask, uint32_t flags)

Open a database. Creates an in-memory map and registers its handle with
the internal file cache, linking it to 'filename'. If a file exists, it
loads the map data for the specified 'database'.

**Parameters**

> *filename Path to file or cache key. NULL → in-memory only. *  
> *database Logical name within file. *  
> *ktype Built-in or registered key type. *  
> *vtype Built-in or registered value type. *  
> *mask Must be 2ⁿ − 1; table size is (mask + 1). *  
> *flags Bitwise OR of QM_AINDEX, QM_MIRROR, QM_SORTED, etc. *

**Returns**

> Map handle (hd).

## uint32_t qmap_put (uint32_t hd, const void \*const key, const void \*const value)

Insert or update a pair.

**Parameters**

> *hd Map handle. *  
> *key Key (NULL if QM_AINDEX). *  
> *value Value to store. *

**Returns**

> Index position for value.

## uint32_t qmap_reg (size_t len)

Register a fixed-length type.

**Parameters**

> *len Length in bytes. *

**Returns**

> Type ID.

## void qmap_save (void)

Write all open maps to disk. Walks the internal cache, computes file
sizes, and performs mmap/memcpy writes.

# Author

Generated automatically by Doxygen for Qmap from the source code.
