//! Android side of the background watcher (`bg_activity`): drives
//! `com.phantomptr.ps5upload.TransferService`, the foreground service that keeps the process
//! (and with it the in-process engine) alive while a transfer runs with the app in the
//! background, and posts the complete / failed notifications. The Kotlin class is written into
//! `gen/android` by `scripts/release/android-postinit-patch.sh`.
//!
//! Every call is best-effort: a failure is logged and the transfer carries on without the
//! service, as it did before it existed.

use jni::objects::{GlobalRef, JClass, JObject, JValue};
use jni::JNIEnv;
use std::sync::OnceLock;

use crate::android_jni::{app_class, app_context, vm};

const SERVICE_CLASS: &str = "com.phantomptr.ps5upload.TransferService";

/// The service class, looked up once through the app's class loader.
static CLASS: OnceLock<GlobalRef> = OnceLock::new();

fn with_service(
    what: &str,
    f: impl for<'a> FnOnce(&mut JNIEnv<'a>, &JClass<'a>, &JObject<'a>) -> jni::errors::Result<()>,
) {
    let result = (|| -> Result<(), String> {
        let mut env = vm()?
            .attach_current_thread()
            .map_err(|e| format!("attach: {e}"))?;
        let out = (|| {
            let context = app_context(&mut env)?;
            let global = match CLASS.get() {
                Some(g) => g,
                None => {
                    let class = app_class(&mut env, &context, SERVICE_CLASS)?;
                    let g = env
                        .new_global_ref(class)
                        .map_err(|e| format!("global ref: {e}"))?;
                    CLASS.get_or_init(|| g)
                }
            };
            let local = env
                .new_local_ref(global.as_obj())
                .map_err(|e| format!("local ref: {e}"))?;
            let class = JClass::from(local);
            f(&mut env, &class, &context).map_err(|e| e.to_string())
        })();
        // Never hand the thread back with a pending exception.
        if env.exception_check().unwrap_or(false) {
            let _ = env.exception_describe();
            let _ = env.exception_clear();
        }
        out
    })();
    if let Err(e) = result {
        eprintln!("[bg] TransferService.{what}: {e}");
    }
}

/// Start the foreground service with its first progress line. `percent` < 0 = indeterminate.
pub fn start(title: &str, text: &str, percent: i32, channel: &str) {
    with_service("start", |env, class, ctx| {
        let (t, x, c) = (
            env.new_string(title)?,
            env.new_string(text)?,
            env.new_string(channel)?,
        );
        env.call_static_method(
            class,
            "start",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V",
            &[
                JValue::Object(ctx),
                JValue::Object(&t),
                JValue::Object(&x),
                JValue::Int(percent),
                JValue::Object(&c),
            ],
        )?;
        Ok(())
    });
}

/// Refresh the ongoing progress notification.
pub fn update(title: &str, text: &str, percent: i32) {
    with_service("update", |env, class, ctx| {
        let (t, x) = (env.new_string(title)?, env.new_string(text)?);
        env.call_static_method(
            class,
            "update",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)V",
            &[
                JValue::Object(ctx),
                JValue::Object(&t),
                JValue::Object(&x),
                JValue::Int(percent),
            ],
        )?;
        Ok(())
    });
}

/// Post a complete / failed notification on the results channel. `id` keeps one per job.
pub fn finished(id: i32, title: &str, text: &str, ok: bool, channel: &str) {
    with_service("finished", |env, class, ctx| {
        let (t, x, c) = (
            env.new_string(title)?,
            env.new_string(text)?,
            env.new_string(channel)?,
        );
        env.call_static_method(
            class,
            "finished",
            "(Landroid/content/Context;ILjava/lang/String;Ljava/lang/String;ZLjava/lang/String;)V",
            &[
                JValue::Object(ctx),
                JValue::Int(id),
                JValue::Object(&t),
                JValue::Object(&x),
                JValue::Bool(ok as u8),
                JValue::Object(&c),
            ],
        )?;
        Ok(())
    });
}

/// Stop the service and remove its progress notification.
pub fn stop() {
    with_service("stop", |env, class, ctx| {
        env.call_static_method(
            class,
            "stop",
            "(Landroid/content/Context;)V",
            &[JValue::Object(ctx)],
        )?;
        Ok(())
    });
}
