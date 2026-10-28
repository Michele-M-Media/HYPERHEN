//! The JavaVM and Application context every Android JNI caller in this crate shares
//! (`commands::local_fs`, `android_bg`). `JNI_OnLoad` may be defined once per library, so it
//! lives here rather than with any one caller.

use jni::objects::{JClass, JObject, JValue};
use jni::{JNIEnv, JavaVM};
use std::ffi::c_void;
use std::sync::OnceLock;

/// JavaVM captured at native-library load.
///
/// Tauri 2's Android backend (a Kotlin WryActivity that `loadLibrary`s
/// this `.so`) does NOT initialize the `ndk_context` crate's global —
/// tao carries its own internal glue and never calls
/// `ndk_context::initialize_android_context`. So the old
/// `ndk_context::android_context()` here always hit an uninitialized
/// global and aborted the whole app on first use ("android context was
/// not initialized"). We capture the VM ourselves in `JNI_OnLoad`
/// instead — the runtime calls it once when the library loads, before
/// any Tauri command can run, and neither wry nor tao defines it.
static JVM: OnceLock<JavaVM> = OnceLock::new();

/// Android runtime entry point, invoked on `System.loadLibrary`.
#[no_mangle]
pub extern "system" fn JNI_OnLoad(
    vm: *mut jni::sys::JavaVM,
    _reserved: *mut c_void,
) -> jni::sys::jint {
    if let Ok(vm) = unsafe { JavaVM::from_raw(vm) } {
        let _ = JVM.set(vm);
    }
    jni::sys::JNI_VERSION_1_6
}

pub(crate) fn vm() -> Result<&'static JavaVM, String> {
    JVM.get()
        .ok_or_else(|| "Android JavaVM unavailable (JNI_OnLoad not run)".to_string())
}

/// The app's Application context — a `Context` we can use for
/// `getSystemService` and `startActivity(FLAG_ACTIVITY_NEW_TASK)`.
/// Fetched via `ActivityThread.currentApplication()` so we don't need a
/// captured Activity (or the unavailable `ndk_context`).
pub(crate) fn app_context<'local>(env: &mut JNIEnv<'local>) -> Result<JObject<'local>, String> {
    let app = env
        .call_static_method(
            "android/app/ActivityThread",
            "currentApplication",
            "()Landroid/app/Application;",
            &[],
        )
        .map_err(|e| format!("currentApplication: {e}"))?
        .l()
        .map_err(|e| format!("application obj: {e}"))?;
    if app.is_null() {
        return Err("currentApplication() returned null".into());
    }
    Ok(app)
}

/// One of the app's own classes (`com.phantomptr.ps5upload.…`). `FindClass` on a thread Rust
/// attached searches the system class loader, which does not have them; the context's loader
/// does. `name` is dotted (`com.example.Foo`).
pub(crate) fn app_class<'local>(
    env: &mut JNIEnv<'local>,
    context: &JObject,
    name: &str,
) -> Result<JClass<'local>, String> {
    let loader = env
        .call_method(context, "getClassLoader", "()Ljava/lang/ClassLoader;", &[])
        .map_err(|e| format!("getClassLoader: {e}"))?
        .l()
        .map_err(|e| format!("loader obj: {e}"))?;
    let jname = env
        .new_string(name)
        .map_err(|e| format!("class name: {e}"))?;
    let class = env
        .call_method(
            &loader,
            "loadClass",
            "(Ljava/lang/String;)Ljava/lang/Class;",
            &[JValue::Object(&jname)],
        )
        .map_err(|e| format!("loadClass {name}: {e}"))?
        .l()
        .map_err(|e| format!("class obj: {e}"))?;
    Ok(JClass::from(class))
}
