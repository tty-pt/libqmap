#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::ffi::{CStr, CString};
use std::ptr;
use libc::{c_char, c_void};

mod bindings;
pub use bindings::*;

pub const QM_MISS: u32 = u32::MAX;

/// Safe wrapper for a Qmap handle.
pub struct Qmap {
    hd: u32,
}

impl Qmap {
    pub fn open(
        filename: Option<&str>,
        database: Option<&str>,
        ktype: u32,
        vtype: u32,
        mask: u32,
        flags: u32,
    ) -> Option<Self> {
        let c_filename = filename.map(|s| CString::new(s).unwrap());
        let c_database = database.map(|s| CString::new(s).unwrap());

        let hd = unsafe {
            qmap_open(
                c_filename.as_ref().map_or(ptr::null(), |s| s.as_ptr()),
                c_database.as_ref().map_or(ptr::null(), |s| s.as_ptr()),
                ktype,
                vtype,
                mask,
                flags,
            )
        };

        if hd == QM_MISS {
            None
        } else {
            Some(Qmap { hd })
        }
    }

    pub fn handle(&self) -> u32 {
        self.hd
    }

    /// Creates a Qmap handle from a raw handle ID.
    /// This should only be used for handles managed elsewhere (like in ndc C backend).
    pub unsafe fn from_handle(hd: u32) -> Self {
        Qmap { hd }
    }

    pub fn get(&self, key: *const c_void) -> *const c_void {
        unsafe { qmap_get(self.hd, key) }
    }

    pub fn get_str(&self, key: &str) -> Option<&str> {
        let c_key = CString::new(key).unwrap();
        let val_ptr = unsafe { qmap_get(self.hd, c_key.as_ptr() as *const c_void) };
        if val_ptr.is_null() {
            None
        } else {
            unsafe {
                CStr::from_ptr(val_ptr as *const c_char).to_str().ok()
            }
        }
    }

    pub fn put(&self, key: *const c_void, value: *const c_void) -> u32 {
        unsafe { qmap_put(self.hd, key, value) }
    }

    pub fn put_str(&self, key: &str, value: &str) -> u32 {
        let c_key = CString::new(key).unwrap();
        let c_value = CString::new(value).unwrap();
        unsafe {
            qmap_put(
                self.hd,
                c_key.as_ptr() as *const c_void,
                c_value.as_ptr() as *const c_void,
            )
        }
    }

    pub fn del(&self, key: *const c_void) {
        unsafe { qmap_del(self.hd, key) }
    }

    pub fn drop(&self) {
        unsafe { qmap_drop(self.hd) }
    }

    pub fn iter(&self, key: *const c_void, flags: u32) -> Cursor {
        let cur_id = unsafe { qmap_iter(self.hd, key, flags) };
        Cursor { cur_id }
    }
}

impl Drop for Qmap {
    fn drop(&mut self) {
        unsafe { qmap_close(self.hd) }
    }
}

pub struct Cursor {
    cur_id: u32,
}

impl Cursor {
    pub fn next(&mut self) -> Option<(*const c_void, *const c_void)> {
        let mut key: *const c_void = ptr::null();
        let mut value: *const c_void = ptr::null();
        let res = unsafe { qmap_next(&mut key, &mut value, self.cur_id) };
        if res == 1 {
            Some((key, value))
        } else {
            None
        }
    }
}

impl Drop for Cursor {
    fn drop(&mut self) {
        unsafe { qmap_fin(self.cur_id) }
    }
}

pub fn save() {
    unsafe { qmap_save() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_qmap_open_in_memory() {
        let q = Qmap::open(None, None, qmap_tbi_QM_STR, qmap_tbi_QM_STR, 0xFF, 0);
        assert!(q.is_some());
        let q = q.unwrap();
        q.put_str("key1", "value1");
        assert_eq!(q.get_str("key1"), Some("value1"));
        assert_eq!(q.get_str("key2"), None);
    }
}
