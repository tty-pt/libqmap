use libc::{c_char, c_int, c_uint, c_void, size_t};

pub const QM_PTR: u32 = 0;
pub const QM_HNDL: u32 = 1;
pub const QM_STR: u32 = 2;
pub const QM_U32: u32 = 3;

pub const QM_AINDEX: u32 = 1;
pub const QM_MIRROR: u32 = 2;
pub const QM_PGET: u32 = 4;
pub const QM_SORTED: u32 = 8;

extern "C" {
    pub fn qmap_open(
        filename: *const c_char,
        database: *const c_char,
        ktype: u32,
        vtype: u32,
        mask: u32,
        flags: u32,
    ) -> u32;

    pub fn qmap_save();
    pub fn qmap_close(hd: u32);
    pub fn qmap_get(hd: u32, key: *const c_void) -> *const c_void;
    pub fn qmap_put(hd: u32, key: *const c_void, value: *const c_void) -> u32;
    pub fn qmap_del(hd: u32, key: *const c_void);
    pub fn qmap_drop(hd: u32);
    pub fn qmap_iter(hd: u32, key: *const c_void, flags: u32) -> u32;
    pub fn qmap_next(key: *mut *const c_void, value: *mut *const c_void, cur_id: u32) -> c_int;
    pub fn qmap_fin(cur_id: u32);
    pub fn qmap_reg(len: size_t) -> u32;
}
