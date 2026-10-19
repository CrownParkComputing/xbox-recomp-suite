#include <jni.h>
#include <climits>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "kernel/disc.h"

namespace {
struct Environment {
    JavaVM* vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    explicit Environment(JavaVM* value) : vm(value) {
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
        }
    }
    ~Environment() { if (attached) vm->DetachCurrentThread(); }
};

std::string String(JNIEnv* env, jstring value) {
    if (!value) throw std::runtime_error("Invalid document entry");
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (!text) throw std::runtime_error("Cannot read document name");
    std::string result(text);
    env->ReleaseStringUTFChars(value, text);
    return result;
}

struct Documents {
    JavaVM* vm;
    jobject reader;
    jmethodID children, open;
    std::vector<std::string> ids;
    size_t count = 0;

    Documents(JNIEnv* env, jobject source) {
        env->GetJavaVM(&vm);
        reader = env->NewGlobalRef(source);
        jclass type = env->GetObjectClass(source);
        children = env->GetMethodID(type, "children", "(Ljava/lang/String;)[Ljava/lang/String;");
        open = env->GetMethodID(type, "open", "(Ljava/lang/String;)I");
        env->DeleteLocalRef(type);
    }
    ~Documents() {
        Environment current(vm);
        if (current.env) current.env->DeleteGlobalRef(reader);
    }

    kernel::Disc::Entry Directory(JNIEnv* env, const std::string& id, std::string name, int depth) {
        if (depth > 32) throw std::runtime_error("Game folder nesting is too deep");
        kernel::Disc::Entry dir{std::move(name), 0, 0, true, {}};
        jstring document = env->NewStringUTF(id.c_str());
        auto rows = static_cast<jobjectArray>(env->CallObjectMethod(reader, children, document));
        env->DeleteLocalRef(document);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            throw std::runtime_error("Cannot read the selected game folder");
        }
        if (!rows) throw std::runtime_error("Cannot list the selected game folder");
        const auto length = env->GetArrayLength(rows);
        if (length % 4) throw std::runtime_error("Invalid game folder listing");
        for (jsize i = 0; i < length; i += 4) {
            if (++count > 500000) throw std::runtime_error("Game folder contains too many entries");
            std::string values[4];
            for (int col = 0; col < 4; ++col) {
                auto value = static_cast<jstring>(env->GetObjectArrayElement(rows, i + col));
                values[col] = String(env, value);
                env->DeleteLocalRef(value);
            }
            if (values[3] == "d") {
                dir.children.push_back(Directory(env, values[0], values[1], depth + 1));
            } else {
                size_t parsed = 0;
                const auto size = std::stoull(values[2], &parsed);
                if (parsed != values[2].size() || size > UINT32_MAX)
                    throw std::runtime_error("A game file is too large for Xbox DVD storage");
                const auto token = ids.size();
                ids.push_back(std::move(values[0]));
                dir.children.push_back({std::move(values[1]), token, uint32_t(size), false, {}});
            }
        }
        env->DeleteLocalRef(rows);
        return dir;
    }

    int Open(uint64_t token) {
        if (token >= ids.size()) return -1;
        Environment current(vm);
        if (!current.env) return -1;
        auto* env = current.env;
        jstring id = env->NewStringUTF(ids[token].c_str());
        const int fd = env->CallIntMethod(reader, open, id);
        env->DeleteLocalRef(id);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
        return fd;
    }
};
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_org_xberecomp_NativeFiles_mountTree(JNIEnv* env, jobject source, jstring root, jstring name) {
    try {
        auto documents = std::make_shared<Documents>(env, source);
        auto entries = documents->Directory(env, String(env, root), "", 0);
        auto disc = std::make_unique<kernel::Disc>();
        std::string error;
        if (!disc->OpenTree(std::move(entries), [documents](uint64_t token) { return documents->Open(token); },
                            String(env, name), error)) throw std::runtime_error(error);
        if (!disc->Find("default.xbe")) throw std::runtime_error("Choose the folder containing default.xbe");
        kernel::MountDisc(std::move(disc));
        return nullptr;
    } catch (const std::exception& error) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return env->NewStringUTF(error.what());
    }
}
