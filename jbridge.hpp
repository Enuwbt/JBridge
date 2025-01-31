#ifndef JBRIDGE_JBRIDGE_HPP
#define JBRIDGE_JBRIDGE_HPP

#include <jni.h>

#include <array>
#include <mutex>
#include <memory>
#include <string_view>
#include <functional>

namespace jb {

    namespace detail {

        template<typename JArrayType, typename ElementType>
        class JPrimitiveArray;

        template<typename MirrorClass>
        class JObjectArray;

        namespace jni {

            template<typename Tp>
            class JniObject;

            inline auto GetEnv() noexcept -> JNIEnv*;

        }
    }

    using BooleanArray  = detail::JPrimitiveArray<jbooleanArray, jboolean>;
    using ByteArray     = detail::JPrimitiveArray<jbyteArray, jbyte>;
    using CharArray     = detail::JPrimitiveArray<jcharArray, jchar>;
    using ShortArray    = detail::JPrimitiveArray<jshortArray, jshort>;
    using IntArray      = detail::JPrimitiveArray<jintArray, jint>;
    using LongArray     = detail::JPrimitiveArray<jlongArray, jlong>;
    using FloatArray    = detail::JPrimitiveArray<jfloatArray, jfloat>;
    using DoubleArray   = detail::JPrimitiveArray<jdoubleArray, jdouble>;

    template<typename MirrorClass>
    using ObjectArray   = detail::JObjectArray<MirrorClass>;

    template<typename Tp>
    using JniObject     = detail::jni::JniObject<Tp>;

    namespace str {

        template<size_t I, size_t J, size_t ...Is, size_t ...Js>
        std::array<char, I + J - 1> operator_add_impl(std::array<char, I> const& lhs, std::array<char, J> const&rhs, std::index_sequence<Is...>, std::index_sequence<Js...>) {
            return { lhs[Is]..., rhs[Js]... };
        }

        template<size_t I, size_t J>
        std::array<char, I + J - 1> operator+(std::array<char, I> const& lhs, std::array<char, J> const& rhs) {
            return operator_add_impl(lhs, rhs, std::make_index_sequence<I - 1>{}, std::make_index_sequence<J>{});
        }

        template<size_t N, size_t ...Ns>
        constexpr std::array<char, N> arrayify_impl(const char (&s)[N], std::index_sequence<Ns...>) {
            return {s[Ns]...};
        }

        template<size_t N>
        constexpr std::array<char, N> arrayify(const char (&s)[N]) {
            return arrayify_impl<N>(s, std::make_index_sequence<N>{});
        }

        template<size_t N, size_t C>
        constexpr std::optional<size_t> find_next(const std::array<char, N>& s, const std::array<char, C>& str, size_t start_pos = 0) {
            if (C == 0 || C > N) {
                return std::nullopt;
            }
            for (; start_pos <= N - C; ++start_pos) {
                size_t j = 0;
                for (; j < C; ++j) {
                    if (s[start_pos + j] != str[j])
                        break;
                }
                if (j == C)
                    return start_pos;
            }
            return std::nullopt;
        }

        template<size_t D, size_t N>
        constexpr auto shrink_array(std::array<char, N> const& s) {
            std::array<char, N - D> d{};
            for (int i = 0; i < N; i++) {
                d[i] = s[i];
            }
            return d;
        }

        template<size_t T, size_t N, size_t ...Ns>
        constexpr auto expand_impl(const char (&s)[N], std::index_sequence<Ns...>) {
            return std::array<char, N + T>{ s[Ns]..., '\0' };
        }

        template<size_t T, size_t N>
        constexpr auto expand(const char (&s)[N]) {
            return expand_impl<T>(s, std::make_index_sequence<N>{});
        }

        template<typename ...Arrays>
        auto add_all(Arrays const& ...arrays) {
            return (arrays + ...);
        }
    }

    namespace detail {

        using namespace str;

        template<typename MirrorType>
        class BaseClass;

        template<size_t N>
        constexpr auto arraysize_of(std::array<char, N> const& arr) {
            return N;
        }

        template<size_t N>
        constexpr auto namespace_depth(std::array<char, N> const& path) {
            int depth = 0;
            for (int i = 0; path[i] != '\0'; i++) {
                if (path[i] == ':' && path[i+1] == ':') {
                    depth++;
                }
            }
            return depth;
        }

        template<size_t M, size_t N>
        constexpr void replace_scope_to_slash(std::array<char, M> &signature, std::array<char, N> const& symbol) {

            constexpr std::array<char, 2> scope_opt = {':', ':'};

            size_t start_pos = 0, prev_pos = 0, write_pos = 0;

            while (auto maybe_pos = find_next(symbol, scope_opt, start_pos)) {
                size_t pos = maybe_pos.value();

                if (pos < prev_pos)
                    break;

                for (size_t i = prev_pos; i < pos; ++i) {
                    signature[write_pos++] = symbol[i];
                }

                signature[write_pos++] = '/';

                prev_pos = pos + scope_opt.size();
                start_pos = prev_pos;
            }

            for (size_t i = prev_pos; i < arraysize_of(symbol); ++i) {
                signature[write_pos++] = symbol[i];
            }
        }

        template<size_t N>
        constexpr std::array<char, N> namespace_to_signature(std::array<char, N> const& arr) {
            std::array<char, N> signature{};
            signature.fill('\0');
            constexpr std::array<char, 2> window = {':', ':'};

            size_t start_pos = 0;
            size_t prev_pos = 0;
            size_t write_pos = 0;
            size_t replaced_cnt = 0;

            while (auto maybe_pos = find_next(arr, window, start_pos)) {
                size_t pos = maybe_pos.value();

                if (pos < prev_pos) {
                    break;
                }

                for (size_t i = prev_pos; i < pos; ++i) {
                    signature[write_pos++] = arr[i];
                }

                signature[write_pos++] = '/';
                replaced_cnt++;

                prev_pos = pos + window.size();
                start_pos = prev_pos;
            }

            for (size_t i = prev_pos; i < N; ++i) {
                signature[write_pos++] = arr[i];
            }

            return signature;
        }

        template<size_t N>
        constexpr std::array<char, N> namespace_to_signature(const char (&s)[N]) {
            return namespace_to_signature(arrayify(s));
        }

        template<bool IsArray, size_t N>
        constexpr auto to_fqcn(std::array<char, N> const& n) {
            constexpr auto arr_prefix = arrayify("[");
            constexpr auto prefix = arrayify("L");
            constexpr auto suffix = arrayify(";");
            if constexpr (IsArray) {
                return add_all(arr_prefix, prefix, n, suffix);
            } else {
                return add_all(prefix, n, suffix);
            }
        };

        template<bool IsArray, size_t N>
        constexpr auto to_fqcn(const char (&n)[N]) {
            return to_fqcn<IsArray>(arrayify(n));
        }

    }

    namespace traits {

        template<class>
        struct deferred_false : std::false_type {};

        template<typename Type>
        class typeof;

        template<class Class>
        inline constexpr bool is_derived_from_jbase = std::is_base_of_v<detail::BaseClass<Class>, Class>;

        template<class Class, typename = std::void_t<>>
        struct is_mirror_class : std::false_type {};

        template<class Class>
        struct is_mirror_class<Class, std::void_t<decltype(Class::CLASS_SIGNATURE)>> : std::true_type {};

        template<class Class>
        inline constexpr bool is_mirror_class_v = is_mirror_class<Class>::value;

        template<typename Type, bool = std::is_array_v<Type>, bool = is_derived_from_jbase<std::remove_extent_t<Type>>>
        struct signature {};

        template<> struct signature<bool, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Z");
        };

        template<> struct signature<jboolean, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Z");
        };

        template<> struct signature<jbyte, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("B");
        };

        template<> struct signature<char, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("C");
        };

        template<> struct signature<jchar, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("C");
        };

        template<> struct signature<jshort, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("S");
        };

        template<> struct signature<jlong, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("J");
        };

        template<> struct signature<jfloat, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("F");
        };

        template<> struct signature<jdouble, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("D");
        };

        template<> struct signature<jint, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("I");
        };

        template<> struct signature<void, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("V");
        };

        template<> struct signature<jbooleanArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[Z");
        };

        template<> struct signature<jboolean[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[Z");
        };

        template<> struct signature<bool[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[Z");
        };

        template<> struct signature<jbyteArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[B");
        };

        template<> struct signature<jbyte[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[B");
        };

        template<> struct signature<jcharArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[C");
        };

        template<> struct signature<jchar[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[C");
        };

        template<> struct signature<char[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[C");
        };

        template<> struct signature<jshortArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[S");
        };

        template<> struct signature<short[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[S");
        };

        template<> struct signature<jintArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[I");
        };

        template<> struct signature<int[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[I");
        };

        template<> struct signature<jlongArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[J");
        };

        template<> struct signature<jlong[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[J");
        };

        template<> struct signature<jfloatArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[F");
        };

        template<> struct signature<jfloat[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[F");
        };

        template<> struct signature<jdoubleArray, false, false> {
            static constexpr auto SIGNATURE = str::arrayify("[D");
        };

        template<> struct signature<jdouble[], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("[D");
        };

        template<size_t N> struct signature<const char (&)[N], false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<size_t N> struct signature<const char (&)[N], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<size_t N>
        struct signature<const char[N], false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<size_t N>
        struct signature<const char[N], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<> struct signature<jstring> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<class Type, bool IsStatic, bool = is_derived_from_jbase<Type>>
        struct jni_field {};

        template<class Type> struct jni_field<Type, false, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> Type {
                return Type(env->GetObjectField(receiver, field_id));
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jobject value) -> void {
                env->SetObjectField(receiver, field_id, value);
            }

        };

        template<class Type> struct jni_field<Type, true, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> Type {
                return Type(env->GetStaticObjectField(cls, field_id));
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jobject value) -> void {
                env->SetStaticObjectField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jboolean, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jboolean {
                return env->GetBooleanField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jboolean value) -> void {
                env->SetBooleanField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jboolean, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jboolean {
                return env->GetStaticBooleanField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jboolean value) -> void {
                env->SetStaticBooleanField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jbyte, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jbyte {
                return env->GetByteField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jbyte value) -> void {
                env->SetByteField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jbyte, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jbyte {
                return env->GetStaticByteField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jbyte value) -> void {
                env->SetStaticByteField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jchar, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jchar {
                return env->GetCharField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jchar value) -> void {
                env->SetCharField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jchar, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jchar {
                return env->GetStaticCharField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jchar value) -> void {
                env->SetStaticCharField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jshort, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jshort {
                return env->GetShortField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jshort value) -> void {
                env->SetShortField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jshort, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jshort {
                return env->GetStaticShortField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jshort value) -> void {
                env->SetStaticShortField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jint, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jint {
                return env->GetIntField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jint value) -> void {
                env->SetIntField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jint, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jint {
                return env->GetStaticIntField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jint value) -> void {
                env->SetStaticIntField(cls, field_id, value);
            }

        };

        template<> struct jni_field<jlong, false> {

            static auto access(JNIEnv *env, jfieldID field_id, jobject receiver) -> jlong {
                return env->GetLongField(receiver, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jobject receiver, jlong value) -> void {
                env->SetLongField(receiver, field_id, value);
            }

        };

        template<> struct jni_field<jlong, true> {

            static auto access(JNIEnv *env, jfieldID field_id, jclass cls) -> jlong {
                return env->GetStaticLongField(cls, field_id);
            }

            static auto set(JNIEnv *env, jfieldID field_id, jclass cls, jlong value) -> void {
                env->SetStaticLongField(cls, field_id, value);
            }

        };


        template<typename Type, bool IsStatic, bool = traits::is_derived_from_jbase<Type>>
        struct jni_call {};

        template<> struct jni_call<bool, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallBooleanMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<bool, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticBooleanMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jboolean, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return jni_call<bool, false>::call(env, method_id, instance, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jboolean, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return jni_call<bool, true>::call(env, method_id, cls, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jbyte, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallByteMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jbyte, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticByteMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<char, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallCharMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<char, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticCharMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jchar, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return jni_call<char, false>::call(env, method_id, instance, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jchar, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return jni_call<char, true>::call(env, method_id, cls, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jshort, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallShortMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jshort, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticShortMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jlong, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallLongMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jlong, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticLongMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jfloat, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallFloatMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jfloat, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticFloatMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jdouble, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallDoubleMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jdouble, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticDoubleMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jint, false> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallIntMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jint, true> {

            template<typename ...Args>
            static bool call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticIntMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<void, false> {

            template<typename ...Args>
            static void call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                env->CallVoidMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<void, true> {

            template<typename ...Args>
            static void call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                env->CallStaticVoidMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jobject, false> {

            template<typename ...Args>
            static jobject call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return env->CallObjectMethod(instance, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jobject, true> {

            template<typename ...Args>
            static jobject call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return env->CallStaticObjectMethod(cls, method_id, std::forward<Args>(args)...);
            }

        };

        template<> struct jni_call<jbooleanArray, false> {

            template<typename ...Args>
            static jbooleanArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jbooleanArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jbooleanArray, true> {

            template<typename ...Args>
            static jbooleanArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jbooleanArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jbyteArray, false> {

            template<typename ...Args>
            static jbyteArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jbyteArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jbyteArray, true> {

            template<typename ...Args>
            static jbyteArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jbyteArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jcharArray, false> {

            template<typename ...Args>
            static jcharArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jcharArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jcharArray, true> {

            template<typename ...Args>
            static jcharArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jcharArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jshortArray, false> {

            template<typename ...Args>
            static jshortArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jshortArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jshortArray, true> {

            template<typename ...Args>
            static jshortArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jshortArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jintArray, false> {

            template<typename ...Args>
            static jintArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jintArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jintArray, true> {

            template<typename ...Args>
            static jintArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jintArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jlongArray, false> {

            template<typename ...Args>
            static jlongArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jlongArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jlongArray, true> {

            template<typename ...Args>
            static jlongArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jlongArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jfloatArray, false> {

            template<typename ...Args>
            static jfloatArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jfloatArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jfloatArray, true> {

            template<typename ...Args>
            static jfloatArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jfloatArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jdoubleArray, false> {

            template<typename ...Args>
            static jdoubleArray call(JNIEnv *env, jmethodID method_id, jobject instance, Args &&...args) {
                return static_cast<jdoubleArray>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }

        };

        template<> struct jni_call<jdoubleArray, true> {

            template<typename ...Args>
            static jdoubleArray call(JNIEnv *env, jmethodID method_id, jclass cls, Args &&...args) {
                return static_cast<jdoubleArray>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }

        };

        template<typename Tp>
        inline constexpr bool is_jni_object_v = std::is_base_of_v<_jobject, std::remove_pointer_t<Tp>>;

        template<typename T>
        struct is_jni_primitive { static constexpr auto value = false; };

        template<typename T>
        inline constexpr auto is_jni_primitive_v = is_jni_primitive<T>::value;

#define SetPrimitiveType(type) template<> struct is_jni_primitive<type> { static constexpr auto value = true; };

        SetPrimitiveType(bool)
        SetPrimitiveType(jboolean)
        SetPrimitiveType(jbyte)
        SetPrimitiveType(char)
        SetPrimitiveType(jchar)
        SetPrimitiveType(jshort)
        SetPrimitiveType(jint)
        SetPrimitiveType(jlong)
        SetPrimitiveType(jfloat)

        template<typename T>
        struct primitive_wrap {};

#define SetPrimitiveWrap(type, class_signature) template<> struct primitive_wrap<type> { static constexpr auto WRAPPER_SIGNATURE = str::arrayify(#class_signature); };

        SetPrimitiveWrap(bool, java/lang/Boolean)
        SetPrimitiveWrap(jboolean, java/lang/Boolean)
        SetPrimitiveWrap(jbyte, java/lang/Byte)
        SetPrimitiveWrap(char, java/lang/Character)
        SetPrimitiveWrap(jchar, java/lang/Character)
        SetPrimitiveWrap(jshort, java/lang/Short)
        SetPrimitiveWrap(jint, java/lang/Integer)
        SetPrimitiveWrap(jlong, java/lang/Long)
        SetPrimitiveWrap(jfloat, java/lang/Float)


        template<typename Type>
        constexpr auto fqcnify() {
            if constexpr (is_derived_from_jbase<std::remove_extent_t<Type>>) {
                return detail::to_fqcn<std::is_array_v<Type>>(std::remove_extent_t<Type>::CLASS_SIGNATURE);
            } else {
                return signature<Type>::SIGNATURE;
            }
        }

        template<typename T>
        struct is_array_wrapper : std::false_type {};

        template<typename Mirror>
        struct is_array_wrapper<ObjectArray<Mirror>> : std::true_type {};

        template<typename ArrayType, typename ElementType>
        struct is_array_wrapper<detail::JPrimitiveArray<ArrayType, ElementType>> : std::true_type {};

        template<class Class>
        inline constexpr bool is_array_wrapper_v = is_array_wrapper<Class>::value;

        template<typename T, bool = is_derived_from_jbase<std::remove_extent_t<T>>, bool = std::is_array_v<T>>
        struct type_validfy {
            using type = T;
        };

        template<typename T>
        struct type_validfy<T, true, false> {
            using type = jobject;
        };

        template<>
        struct type_validfy<jboolean[], false, true> {
            using type = jbooleanArray;
        };

        template<>
        struct type_validfy<bool[], false, true> {
            using type = jbooleanArray;
        };

        template<>
        struct type_validfy<jbyte[], false, true> {
            using type = jbyteArray;
        };

        template<>
        struct type_validfy<jchar[], false, true> {
            using type = jcharArray;
        };

        template<>
        struct type_validfy<char[], false, true> {
            using type = jcharArray;
        };

        template<>
        struct type_validfy<short[], false, true> {
            using type = jshortArray;
        };

        template<>
        struct type_validfy<jint[], false, true> {
            using type = jintArray;
        };

        template<>
        struct type_validfy<jlong[], false, true> {
            using type = jlongArray;
        };

        template<>
        struct type_validfy<jfloat[], false, true> {
            using type = jfloatArray;
        };

        template<>
        struct type_validfy<jdouble[], false, true> {
            using type = jdoubleArray;
        };

        template<typename T>
        struct type_validfy<T, true, true> {
            using type = typename std::remove_extent_t<T>::ArrayType;
        };

        template<typename Mirror>
        struct type_validfy<ObjectArray<Mirror>, false, false> {
            using type = jobject;
        };

        template<typename T>
        using type_validfy_t = typename type_validfy<T>::type;

        template<typename T, bool = is_mirror_class_v<std::remove_extent_t<T>>, bool = std::is_array_v<T>>
        struct array_wrapper {
            using type = T;
        };

        template<typename T>
        struct array_wrapper<T, true, true> {
            using type = typename std::remove_extent_t<T>::ArrayType;
        };
        
        template<>
        struct array_wrapper<void> {
            using type = void;
        };

        template<>
        struct array_wrapper<jboolean[]> {
            using type = BooleanArray;
        };

        template<>
        struct array_wrapper<bool[]> {
            using type = BooleanArray;
        };

        template<>
        struct array_wrapper<jbyte[]> {
            using type = ByteArray;
        };

        template<>
        struct array_wrapper<jchar[]> {
            using type = CharArray;
        };

        template<>
        struct array_wrapper<char[]> {
            using type = CharArray;
        };

        template<>
        struct array_wrapper<short[]> {
            using type = ShortArray;
        };

        template<>
        struct array_wrapper<jint[]> {
            using type = IntArray;
        };

        template<>
        struct array_wrapper<jlong[]> {
            using type = LongArray;
        };

        template<>
        struct array_wrapper<jfloat[]> {
            using type = FloatArray;
        };

        template<>
        struct array_wrapper<jdouble[]> {
            using type = DoubleArray;
        };

        template<typename T>
        using array_wrapper_t = typename array_wrapper<T>::type;

        template<typename T>
        struct is_const_chars_ref : std::false_type {};

        template<size_t N> struct is_const_chars_ref<const char (&)[N]> : std::true_type {};

        template<typename T>
        constexpr bool is_const_chars_ref_v = is_const_chars_ref<T>::value;

    }

    namespace tokenizer {

        using namespace traits;
        using namespace detail;

        template<typename ...Types>
        constexpr auto build_param_signature() {
            return str::add_all(str::arrayify("("), traits::fqcnify<Types>()..., str::arrayify(")"));
        }

        template<typename ReturnType>
        constexpr auto build_return_signature() {
            return traits::fqcnify<ReturnType>();
        }

        template<typename ReturnType, typename ...Types>
        constexpr auto build_function_signature() {
            return str::add_all(build_param_signature<Types...>(), build_return_signature<ReturnType>());
        }

    }

    namespace detail {

        constexpr bool Static = true;
        constexpr bool NonStatic = false;

        namespace jni {

            inline static JavaVM *vm_;

            inline auto GetEnv() noexcept -> JNIEnv* {

                struct Attacher {

                    explicit Attacher() {
                        vm_->AttachCurrentThread(&_, nullptr);
                    }

                    ~Attacher() {
                        vm_->DetachCurrentThread();
                    }

                    JNIEnv *_{};
                } thread_local attacher;

                return attacher._;
            }

            inline auto GetDefaultConstructor(JNIEnv *env, jclass cls) {
                return env->GetMethodID(cls, "<init>", "()V");
            }

            template<typename RefType>
            inline auto MakeGlobalRef(JNIEnv *env, jobject ref) {
                return static_cast<RefType>(env->NewGlobalRef(ref));
            }

            template<typename RefType>
            inline auto DeleteGlobalRef(JNIEnv *env, RefType ref) {
                if (env->GetObjectRefType(ref) == JNIGlobalRefType) {
                    env->DeleteGlobalRef(ref);
                }
            }

            inline void DeleteGlobalRefWithoutJNIEnv(jobject ref) {
                DeleteGlobalRef(GetEnv(), ref);
            }

            template<size_t N>
            inline auto FindClass(std::array<char, N> const& class_signature) {
                return GetEnv()->FindClass(class_signature.data());
            }

            template<typename T>
            inline auto JObjectify(T &&t) -> jobject {

                if constexpr (traits::is_jni_primitive_v<T>) {
                    using TWrapped = traits::primitive_wrap<T>;
                    auto env = detail::jni::GetEnv();
                    auto cls = env->FindClass(TWrapped::WRAPPER_SIGNATURE.data());
                    static auto methodid_value = env->GetStaticMethodID(cls, "valueOf",
                                                                        str::add_all(
                                                                                str::arrayify("("),
                                                                                traits::signature<T>::SIGNATURE,
                                                                                str::arrayify(")"),
                                                                                detail::to_fqcn<false>(TWrapped::WRAPPER_SIGNATURE)).data());
                    return env->CallStaticObjectMethod(cls, methodid_value, t);
                } else if constexpr (traits::is_derived_from_jbase<T>) {
                    return t.GetObject();
                } else if constexpr (std::is_base_of_v<std::remove_pointer_t<T>, _jobject>) {
                    return t;
                }
            }

            template<typename T>
            inline auto Validfy(T &&t) {
                using ArgType = std::remove_reference_t<std::remove_reference_t<T>>;

                if constexpr (traits::is_jni_primitive_v<ArgType> || std::is_base_of_v<std::remove_pointer_t<ArgType>, _jobject>) {
                    return t;
                } else if constexpr (traits::is_derived_from_jbase<ArgType>) {
                    return t.GetObject();
                } else if constexpr (std::is_same_v<ArgType, std::string> || std::is_same_v<ArgType, std::string_view>) {
                    return detail::jni::GetEnv()->NewStringUTF(t.data());
                } else if constexpr (traits::is_const_chars_ref_v<T>) {
                    return detail::jni::GetEnv()->NewStringUTF(t);
                } else if constexpr (traits::is_array_wrapper_v<T>) {
                    return t.Raw();
                } else {
                    return t;
                }
            }

            template<typename ElementType>
            auto NewPrimitiveArray(JNIEnv *env, size_t length) {
                if constexpr (std::is_same_v<ElementType, jboolean>) {
                    return env->NewBooleanArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jbyte>) {
                    return env->NewByteArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jchar>) {
                    return env->NewCharArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jshort>) {
                    return env->NewShortArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jint>) {
                    return env->NewIntArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jlong>) {
                    return env->NewLongArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jfloat>) {
                    return env->NewFloatArray(static_cast<jsize>(length));
                }
                else if constexpr (std::is_same_v<ElementType, jdouble>) {
                    return env->NewDoubleArray(static_cast<jsize>(length));
                }
                else {
                    static_assert(traits::deferred_false<ElementType>::value, "Unsupported JNI array type");
                }
            }

            template<typename JArrayType>
            auto GetArrayElements(JNIEnv *env, JArrayType array) {
                if constexpr (std::is_same_v<JArrayType, jbooleanArray>) {
                    return env->GetBooleanArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jbyteArray>) {
                    return env->GetByteArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jcharArray>) {
                    return env->GetCharArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jshortArray>) {
                    return env->GetShortArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jintArray>) {
                    return env->GetIntArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jlongArray>) {
                    return env->GetLongArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jfloatArray>) {
                    return env->GetFloatArrayElements(array, nullptr);
                }
                else if constexpr (std::is_same_v<JArrayType, jdoubleArray>) {
                    return env->GetDoubleArrayElements(array, nullptr);
                }
                else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI array type");
                }
            }

            template<typename JArrayType, typename ElementType>
            void ReleaseArrayRegion(JNIEnv *env, JArrayType array, ElementType elements) {
                if constexpr (std::is_same_v<JArrayType, jbooleanArray>) {
                    env->ReleaseBooleanArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jbyteArray>) {
                    env->ReleaseByteArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jcharArray>) {
                    env->ReleaseCharArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jshortArray>) {
                    env->ReleaseShortArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jintArray>) {
                    env->ReleaseIntArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jlongArray>) {
                    env->ReleaseLongArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jfloatArray>) {
                    env->ReleaseFloatArrayElements(array, elements, 0);
                }
                else if constexpr (std::is_same_v<JArrayType, jdoubleArray>) {
                    env->ReleaseDoubleArrayElements(array, elements, 0);
                }
                else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI array type");
                }
            }

            template<typename JArrayType, typename ElementType>
            void SetArrayRegion(JNIEnv *env, JArrayType &array, jsize start, jsize len, const ElementType buf) {
                if constexpr (std::is_same_v<JArrayType, jbooleanArray>) {
                    env->SetBooleanArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jbyteArray>) {
                    env->SetByteArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jcharArray>) {
                    env->SetCharArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jshortArray>) {
                    env->SetShortArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jintArray>) {
                    env->SetIntArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jlongArray>) {
                    env->SetLongArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jfloatArray>) {
                    env->SetFloatArrayRegion(array, start, len, buf);
                }
                else if constexpr (std::is_same_v<JArrayType, jdoubleArray>) {
                    env->SetDoubleArrayRegion(array, start, len, buf);
                }
                else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI type");
                }
            }

            static constexpr auto kJniTag = 0xECD8000000000000;

            static auto IsEncoded(uintptr_t reference) -> bool {
                return (reference & kJniTag) == kJniTag;
            }

            template<typename Tp>
            static auto Encode(Tp reference) -> uintptr_t {
                return reinterpret_cast<uintptr_t>(reference) | kJniTag;
            }

            template<typename Tp>
            static auto Decode(uintptr_t encoded) -> Tp {
                return reinterpret_cast<Tp>(encoded & ~kJniTag);
            }

            template<typename Tp>
            class JniObject {
            public:

                JniObject() : reference_(0) {}

                JniObject(Tp ptr) : reference_(Encode(reinterpret_cast<uintptr_t>(ptr))) {}

                JniObject(JniObject const& o) : reference_(IsEncoded(o.reference_) ? o.reference_ : Encode(o.reference_)) {}

                JniObject& operator=(JniObject const& o) {

                    if (&o == this)
                        return *this;

                    if (IsEncoded(o.reference_)) {
                        reference_ = o.reference_;
                    } else {
                        reference_ = Encode(o.reference_);
                    }

                    return *this;
                }

                auto Get() {
                    return Decode<Tp>(reference_);
                }

                auto Set(Tp ptr) -> void {
                    reference_ = Encode(reinterpret_cast<uintptr_t>(ptr));
                }

            private:

                uintptr_t reference_;

            };

            template<typename Tp>
            class JniRef {

                static constexpr size_t FieldSize = sizeof(Tp);
                static constexpr size_t PointerSize = sizeof(void*);

            private:

                static auto SearchJniObjectOnField(void *base, std::function<void (JNIEnv*, uintptr_t&)> const& callback) {
                    auto env = GetEnv();
                    auto start = reinterpret_cast<uintptr_t>(base);
                    for (size_t off = 0; off + PointerSize <= FieldSize; off += PointerSize) {
                        auto& value = *reinterpret_cast<uintptr_t*>(start + off);
                        if (IsEncoded(value)) {
                            callback(env, value);
                        }
                    }
                }

            public:

                static auto Promote(Tp *base) -> void {
                    SearchJniObjectOnField(base, [](JNIEnv *env, uintptr_t &value) {
                        if (IsEncoded(value)) {
                            value = Encode(env->NewGlobalRef(Decode<jobject>(value)));
                        }
                    });
                }

                static auto Demote(Tp *base) -> void {
                    SearchJniObjectOnField(base, [](JNIEnv *env, const uintptr_t &value) {
                        if (IsEncoded(value)) {
                            env->DeleteGlobalRef(Decode<jobject>(value));
                        }
                    });
                }
            };
        };

        template<typename MirrorType>
        class BaseClass {
        public:

            using ArrayType = ObjectArray<MirrorType>;

            explicit BaseClass() noexcept : env_(jni::GetEnv()),
                                            declaring_class_(env_->FindClass(MirrorType::CLASS_SIGNATURE.data())),
                                            object_([this]{
                                                static jmethodID default_constructor = env_->GetMethodID(declaring_class_.Get(), "<init>", "()V");
                                                return env_->NewObject(declaring_class_.Get(), default_constructor);
                                            }()) {
            }

            BaseClass(jobject instance) noexcept : env_(jni::GetEnv()),
                                                            declaring_class_(env_->FindClass(MirrorType::CLASS_SIGNATURE.data())),
                                                            object_(instance) {}

            BaseClass(BaseClass const& o) noexcept = default;

            BaseClass(BaseClass&& o) noexcept = default;

            BaseClass& operator=(BaseClass const& o) noexcept = default;

            BaseClass& operator=(BaseClass&& o) = default;

            auto GetObject() -> jobject {
                return object_.Get();
            }

            auto GetDeclaringClass() -> jclass {
                return declaring_class_.Get();
            }

        protected:

            JNIEnv *env_{};
            jni::JniObject<jclass> declaring_class_;
            jni::JniObject<jobject> object_;
        };

        class Constructor {
        public:

            Constructor(jmethodID declaring_ctor) : declaring_ctor_(declaring_ctor) {}

            template<typename ...Args>
            auto call(jclass cls, Args &&... args) {
                return jni::GetEnv()->NewObject(cls, declaring_ctor_, jni::Validfy(std::forward<Args>(args))...);
            }

        private:

            jmethodID declaring_ctor_;

        };

        template<typename ReturnType>
        class Method {
        public:

            Method(jmethodID declaring_method) : declaring_method_(declaring_method) {}

            template<bool IsStatic, typename ...Args>
            auto call(jclass cls, Args &&...args) -> std::enable_if_t<IsStatic, traits::type_validfy_t<ReturnType>> {
                if constexpr (std::is_void_v<traits::type_validfy_t<ReturnType>>) {
                    traits::jni_call<traits::type_validfy_t<ReturnType>, true>::call(jni::GetEnv(), declaring_method_, cls, jni::Validfy(std::forward<Args>(args))...);
                } else {
                    //Double type_validfy_t means: Mirror[] -> WrappedArray -> jobject
                    return std::move(traits::jni_call<traits::type_validfy_t<
                            traits::type_validfy_t<ReturnType>>, true>::call(jni::GetEnv(), declaring_method_, cls, jni::Validfy(std::forward<Args>(args))...));
                }
            }

            template<bool IsStatic, typename ...Args>
            auto call(jobject object, Args &&...args) -> std::enable_if_t<!IsStatic, traits::type_validfy_t<ReturnType>> {
                if constexpr (std::is_void_v<traits::type_validfy_t<ReturnType>>) {
                    traits::jni_call<traits::type_validfy_t<ReturnType>, false>::call(jni::GetEnv(), declaring_method_, object, jni::Validfy(std::forward<Args>(args))...);
                } else {
                    //Double type_validfy_t means: Mirror[] -> WrappedArray -> jobject
                    return std::move(traits::jni_call<
                            traits::type_validfy_t<
                                    traits::type_validfy_t<ReturnType>>, false>::call(jni::GetEnv(), declaring_method_, object, jni::Validfy(std::forward<Args>(args))...));
                }
            }

        private:

            jmethodID declaring_method_{};

        };

        template<typename ...ArgsTypes, size_t N>
        static auto CreateConstructor(std::array<char, N> const& class_signature,
                                      JNIEnv *env = jni::GetEnv()) -> Constructor {
            return {
                env->GetMethodID(
                        env->FindClass(class_signature.data()),
                        "<init>",
                        tokenizer::build_function_signature<void, std::remove_reference_t<ArgsTypes>...>().data())
            };
        }

        template<bool IsStatic, typename ReturnType, typename ...ParameterTypes, size_t N>
        static auto CreateMethod(std::array<char, N> const& class_signature,
                                 std::string_view name,
                                 JNIEnv *env = jni::GetEnv()) -> Method<ReturnType> {

            if constexpr (IsStatic) {
                return {
                    env->GetStaticMethodID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        tokenizer::build_function_signature<ReturnType, ParameterTypes...>().data()
                    )
                };
            } else {
                return {
                    env->GetMethodID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        tokenizer::build_function_signature<ReturnType, ParameterTypes...>().data()
                    )
                };
            }
        }

        template<bool IsStatic, typename FieldType>
        class Field {};

        template<typename FieldType>
        class Field<Static, FieldType> {
        public:

            Field(jfieldID field_id) : declaring_field_(field_id) {}

            Field(Field const& o) = default;

            Field& operator=(Field const& o) = delete;

            Field(Field&& o) = default;

            Field& operator=(Field&& o) = delete;

            Field& operator=(FieldType value) {
                Set(value);
                return *this;
            }

            operator FieldType() noexcept {
                return Get();
            }

            auto Update(jclass ref) -> Field {
                this->class_ref_ = ref;
                return std::move(*this);
            }

            auto Get() -> FieldType {
                return traits::jni_field<FieldType, Static>::access(jni::GetEnv(), declaring_field_, class_ref_);
            }

            auto Set(FieldType value) -> void {
                traits::jni_field<FieldType, Static>::set(jni::GetEnv(), declaring_field_, class_ref_, value);
            }

            auto operator()() -> FieldType {
                return Get();
            }

        private:

            jfieldID declaring_field_{};
            jclass class_ref_{};

        };

        template<typename FieldType>
        class Field<NonStatic, FieldType> {
        public:

            Field(jfieldID field_id) : declaring_field_(field_id) {}

            Field(Field const& o) = default;

            Field& operator=(Field const& o) = delete;

            Field(Field&& o) = default;

            Field& operator=(Field&& o) = delete;

            Field& operator=(FieldType value) {
                Set(value);
                return *this;
            }

            operator FieldType() noexcept {
                return Get();
            }

            auto Update(jobject ref) -> Field& {
                this->object_ref_ = ref;
                return *this;
            }

            auto Get() const -> FieldType {
                return traits::jni_field<FieldType, NonStatic>::access(jni::GetEnv(), declaring_field_, object_ref_);
            }

            auto Set(FieldType val) const -> void {
                traits::jni_field<FieldType, NonStatic>::set(jni::GetEnv(), declaring_field_, object_ref_, val);
            }

            auto operator()() -> FieldType {
                return Get();
            }

        private:

            jfieldID declaring_field_{};
            jobject object_ref_{};

        };

        template<bool IsStatic, typename Type, size_t N>
        static auto CreateField(std::array<char, N> const& class_signature, std::string_view name, JNIEnv *env = jni::GetEnv()) -> Field<IsStatic, Type> {
            if constexpr (IsStatic) {
                return {
                    env->GetStaticFieldID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        traits::fqcnify<Type>().data()
                    )
                };
            } else {
                return {
                    env->GetFieldID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        traits::fqcnify<Type>().data()
                    )
                };
            }
        }

        template<typename JArrayType, typename ElementType>
        class JPrimitiveArray {
        public:

            JPrimitiveArray() {}

            JPrimitiveArray(size_t initial_capacity) : env_(jni::GetEnv()),
                                                       array_(jni::NewPrimitiveArray<ElementType>(env_, initial_capacity)),
                                                       elements_(nullptr) {
                elements_ = jni::GetArrayElements(env_, array_.Get());
                size_ = env_->GetArrayLength(array_.Get());
            }

            JPrimitiveArray(JArrayType array) : env_(jni::GetEnv()),
                                                array_(array),
                                                elements_(nullptr) {
                elements_ = jni::GetArrayElements(env_, array);
                size_ = env_->GetArrayLength(array);
            }

            JPrimitiveArray(jobject array) : JPrimitiveArray(static_cast<JArrayType>(array)) {}

            JPrimitiveArray(JPrimitiveArray const& o) : env_(jni::GetEnv()),
                                                        array_(jni::NewPrimitiveArray<ElementType>(env_, o.size_)),
                                                        elements_(nullptr),
                                                        size_(o.size_){
                elements_ = jni::GetArrayElements(env_, array_.Get());
                auto other_elements = jni::GetArrayElements(env_, o.array_.Get());
                for (size_t i = 0; i < size_; ++i) {
                    elements_[i] = other_elements[i];
                }

                jni::ReleaseArrayRegion(env_, o.array_.Get(), other_elements);

            }

            JPrimitiveArray& operator=(JPrimitiveArray const& o) {
                if (this == &o)
                    return *this;

                std::scoped_lock lock(mutex_);

                Release();

                array_ = {jni::NewPrimitiveArray<ElementType>(env_, o.size_)};
                size_ = o.size_;
                auto other_elements = jni::GetArrayElements(env_, o.array_.Get());
                elements_ = jni::GetArrayElements(env_, array_.Get());

                for (size_t i = 0; i < size_; ++i) {
                    elements_[i] = other_elements[i];
                }

                jni::ReleaseArrayRegion(env_, o.array_.Get(), o.elements_);

                return *this;
            };

            JPrimitiveArray(JPrimitiveArray &&o) noexcept : env_(o.env_),
                                                             elements_(o.elements_),
                                                             array_(o.array_.Get()),
                                                             size_(o.size_),
                                                             mutex_() {
                o.elements_ = nullptr;
                o.size_ = 0;
            }

            JPrimitiveArray& operator=(JPrimitiveArray &&o) noexcept {
                if (this == &o)
                    return *this;

                std::scoped_lock lock(mutex_);

                Release();

                env_ = o.env_;
                elements_ = o.elements_;
                array_ = {o.array_.Get()};
                size_ = o.size_;

                o.elements_ = nullptr;
                o.size_ = 0;

                return *this;
            };

            ~JPrimitiveArray() {
                Release();
            }

            auto operator[](size_t index) -> ElementType& {
                return Get(index);
            }

            auto operator[](size_t index) const -> ElementType {
                return Get(index);
            }

            auto Get(size_t index) -> ElementType {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("index out of bounds in Get()");

                return elements_[index];
            }

            auto Get(size_t index) const -> ElementType {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("index out of bounds in Get()");

                return elements_[index];
            }

            auto Set(size_t index, ElementType element) {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("index out");
                elements_[index] = element;
            }

            auto Size() const {
                std::scoped_lock lock(mutex_);
                return size_;
            }

            auto Raw() const -> JArrayType {
                return array_.Get();
            }

            auto begin() -> ElementType* {
                return elements_;
            }

            auto end() -> ElementType* {
                return elements_ + size_;
            }

        private:

            void Release() {
                if (elements_) {
                    jni::ReleaseArrayRegion(env_, array_.Get(), elements_);
                    elements_ = nullptr;
                }
            }

        private:

            JNIEnv *env_;
            ElementType *elements_;
            detail::jni::JniObject<JArrayType> array_;
            size_t size_ = 0;
            mutable std::mutex mutex_;

        };

        template<typename MirrorClass>
        class JObjectArray {

            static constexpr size_t kDefaultCapacity = 10;

        public:

            JObjectArray() : JObjectArray(kDefaultCapacity) {}

            JObjectArray(size_t size) : env_(detail::jni::GetEnv()),
                                       size_(size),
                                       class_(env_->FindClass(MirrorClass::CLASS_SIGNATURE.data())),
                                       array_(env_->NewObjectArray(size, class_.Get(), nullptr)) {}

            JObjectArray(jobjectArray array) : env_(detail::jni::GetEnv()),
                                              size_(env_->GetArrayLength(array)),
                                              class_(env_->FindClass(MirrorClass::CLASS_SIGNATURE.data())),
                                              array_(static_cast<jobjectArray>(array)) {}

            JObjectArray(jobject array) : JObjectArray(static_cast<jobjectArray>(array)) {}

            JObjectArray(JObjectArray const& o) : env_(o.env_),
                                                size_(o.size_),
                                                class_(o.class_),
                                                array_(o.array_),
                                                mutex_() {}

            JObjectArray& operator=(JObjectArray const& o) {
                if (this == &o)
                    return *this;

                env_ = o.env_;
                size_ = o.size_;
                class_ = o.class_;
                array_ = o.array_;
                mutex_;

                return *this;
            }

            auto operator[](size_t index) -> MirrorClass {
                return Get(index);
            }

            auto operator[](size_t index) const -> MirrorClass {
                return Get(index);
            }

            auto Get(size_t index) -> MirrorClass {
                return GetAsRaw(index);
            }

            auto GetAsRaw(size_t index) -> jobject {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("index out of bounds in Get()");

                return env_->GetObjectArrayElement(array_.Get(), index);
            }

            auto Set(size_t index, jobject element) {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("index out of bounds in Set()");

                env_->SetObjectArrayElement(array_.Get(), index, element);
            }

            [[nodiscard]] auto Size() -> size_t {
                std::scoped_lock lock(mutex_);
                return size_;
            }

            auto Raw() -> jobjectArray {
                return array_.Get();
            }

        private:

            JNIEnv *env_;
            size_t size_;
            detail::jni::JniObject<jobjectArray> array_;
            detail::jni::JniObject<jclass> class_;
            std::mutex mutex_;

        };
    };

    template<typename Tp>
    inline auto MakeGlobalRef(Tp &object) -> std::enable_if_t<!jb::traits::is_jni_object_v<Tp>, std::shared_ptr<Tp>> {
        using namespace detail::jni;
        JniRef<Tp>::Promote(&object);
        return {&object, JniRef<Tp>::Demote};
    }

    template<typename Op>
    inline auto MakeGlobalRef(Op object) -> std::enable_if_t<jb::traits::is_jni_object_v<Op>, std::shared_ptr<std::remove_pointer_t<Op>>> {
        using namespace detail::jni;
        return {MakeGlobalRef<Op>(GetEnv(), object), DeleteGlobalRefWithoutJNIEnv};
    }


    inline auto Init(JavaVM *vm) noexcept -> void {
        detail::jni::vm_ = vm;
    }

}

#define JBRIDGE_DEFINE_CLASS(package, class_name, class_scope)                                                      \
namespace package {                                                                                                 \
                                                                                                                    \
    template<typename D>                                                                                                                \
    struct _M_base_ ## class_name {                                                                             \
                                                                                                                    \
        static constexpr auto CLASS_SIGNATURE = [] {                                                                \
                                                                                                                    \
            using namespace jb::detail;                                                                             \
                                                                                                                    \
            auto symbol = arrayify(#package "::" #class_name);                                                      \
            constexpr auto symbol_size = arraysize_of(symbol);                                                     \
            std::array<char, symbol_size - namespace_depth(arrayify(#package "::" #class_name))> signature{};       \
                                                                                                                    \
            replace_scope_to_slash(signature, symbol);                                                              \
                                                                                                                    \
            return signature;                                                                                       \
        }();                                                                                                        \
                                                                                                                    \
        template<typename ...Args>                                                                                  \
        static auto new_(Args &&...args) -> D {                                                                     \
            static auto constructor = jb::detail::CreateConstructor<Args...>(CLASS_SIGNATURE);                      \
            return D{constructor.call(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...)};    \
        }                                                                                                           \
                                                                                                                    \
    };                                                                                                              \
                                                                                                                    \
    struct class_name : public jb::detail::BaseClass<class_name>, public _M_base_ ## class_name<class_name> class_scope;        \
                                                                                                                    \
}

#define JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(class_name) using jb::detail::BaseClass<class_name>::BaseClass;

#define JBRIDGE_DEFINE_METHOD(return_type, name, ...) \
template<typename ...Args>                                              \
auto name(Args &&...args) -> jb::traits::array_wrapper_t<return_type> {            \
    static auto name ## _ = jb::detail::CreateMethod<false, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name);\
    return name ## _.call<false>(object_.Get(), std::forward<Args>(args)...);                                            \
}

#define JBRIDGE_DEFINE_ALIAS_METHOD(return_type, alias_name, name, ...) \
template<typename ...Args>                                              \
auto alias_name(Args &&...args) -> jb::traits::array_wrapper_t<return_type> {            \
    static auto name ## _ = jb::detail::CreateMethod<false, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name);\
    return name ## _.call<false>(object_.Get(), std::forward<Args>(args)...);                                            \
}

#define JBRIDGE_DEFINE_STATIC_METHOD(return_type, name, ...) \
template<typename ...Args>                                              \
static auto name(Args &&...args) -> jb::traits::array_wrapper_t<return_type> {            \
    static auto name ## _ = jb::detail::CreateMethod<true, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name);\
    return name ## _.call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...);                                            \
}

#define JBRIDGE_DEFINE_STATIC_ALIAS_METHOD(return_type, alias_name, name, ...) \
template<typename ...Args>                                              \
static auto alias_name(Args &&...args) -> jb::traits::array_wrapper_t<return_type> {            \
    static auto name ## _ = jb::detail::CreateMethod<true, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name);\
    return name ## _.call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...);                                            \
}

#define JBRIDGE_DEFINE_FIELD(field_type, name) \
decltype(auto) name() {                \
    static auto field ## _ = jb::detail::CreateField<false, jb::traits::array_wrapper_t<field_type>>(CLASS_SIGNATURE, #name);    \
    return field ## _ . Update(object_.Get());                                                            \
}

#define JBRIDGE_DEFINE_STATIC_FIELD(field_type, name) \
static decltype(auto) name() {                \
    static auto field ## _ = jb::detail::CreateField<true, jb::traits::array_wrapper_t<field_type>>(CLASS_SIGNATURE, #name);    \
    return field ## _ . Update(jb::detail::jni::FindClass(CLASS_SIGNATURE));                                                            \
}


#endif //JBRIDGE_JBRIDGE_HPP
