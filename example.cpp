#include <jni.h>
#include <string>
#include <memory>

#include <android/log.h>
#include <elf.h>

#include <cassert>

#include "jbridge_out.hpp"


#include "jbridge/jbridge.hpp"


JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    jb::Init(vm);
    return JNI_VERSION_1_6;
}

JBRIDGE_DEFINE_CLASS(java::lang, CharSequence, {})

JBRIDGE_DEFINE_CLASS(java::lang, String, {})

JBRIDGE_DEFINE_CLASS(android::widget, TextView, {

    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(TextView);

    JBRIDGE_DEFINE_STATIC_FIELD(int, PROCESS_TEXT_REQUEST_CODE)

    JBRIDGE_DEFINE_METHOD(void, setText, java::lang::CharSequence)

})

extern "C"
JNIEXPORT void JNICALL
Java_rec_enuwbt_jbridge_MainActivity_testSetText(JNIEnv *env, jclass clazz, jobject text_view) {
    auto tv = android::widget::TextView(text_view);
    tv.setText("Hello with jbridge");
}


JBRIDGE_DEFINE_CLASS(java::io, PrintStream, {

    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(PrintStream)

    JBRIDGE_DEFINE_METHOD(void, println, java::lang::String)

    JBRIDGE_DEFINE_ALIAS_METHOD(void, printlnInt, println, int)

})

JBRIDGE_DEFINE_CLASS(java::lang, System, {

    JBRIDGE_DEFINE_STATIC_FIELD(java::io::PrintStream, out)

})

extern "C"
JNIEXPORT void JNICALL
Java_rec_enuwbt_jbridge_MainActivity_testSystemOut(JNIEnv *env, jclass clazz) {

    using namespace java::lang;

    System::out()().println("System out println!");

}

JBRIDGE_DEFINE_CLASS(test, TestClass, {

    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(TestClass);

    JBRIDGE_DEFINE_METHOD(java::lang::String, getName)

    JBRIDGE_DEFINE_METHOD(void, printName)

    JBRIDGE_DEFINE_STATIC_METHOD(int[], magicNumbers)

})

extern "C"
JNIEXPORT void JNICALL
Java_rec_enuwbt_jbridge_MainActivity_testHandleTestClass(JNIEnv *env, jclass clazz) {

    using namespace java::lang;

    auto test_class = test::TestClass::new_("tester");
    test_class.printName();

    for (auto v : test::TestClass::magicNumbers()) {

        System::out()().printlnInt(v);

    }
}
extern "C"
JNIEXPORT void JNICALL
Java_rec_enuwbt_jbridge_MainActivity_testMakeTestClassGlobalRef(JNIEnv *env, jclass clazz) {

    using namespace java::lang;

    auto test_class = test::TestClass::new_("global tester");

    auto global_test_class = jb::MakeGlobalRef(test_class);

    global_test_class->printName();

    assert(env->GetObjectRefType(global_test_class->GetObject()) == JNIGlobalRefType);

} // auto deref global test class

extern "C"
JNIEXPORT void JNICALL
Java_rec_enuwbt_jbridge_MainActivity_testMakeObjectGlobalRef(JNIEnv *env, jclass clazz) {

    jclass class_object = env->FindClass("java/lang/Object");

    auto global_class_object = jb::MakeGlobalRef(class_object);

    assert(env->GetObjectRefType(global_class_object.get()) == JNIGlobalRefType);

} // auto delete global ref because of shared_ptr