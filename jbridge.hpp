#ifndef JBRIDGE_JBRIDGE_HPP
#define JBRIDGE_JBRIDGE_HPP

#include <jni.h>

#include <array>
#include <mutex>
#include <memory>
#include <string_view>
#include <functional>
#include <concepts>
#include <type_traits>
#include <stdexcept>
#include <span>
#include <optional>
#include <utility>
#include <cstdint>

namespace jb {

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    namespace detail {

        template<typename JArrayType, typename ElementType>
        class JPrimitiveArray;

        template<typename MirrorClass>
        class JObjectArray;

        template<typename MirrorType>
        class BaseClass;

        namespace jni {

            template<typename Tp>
            class JniObject;

            [[nodiscard]] inline auto GetEnv() noexcept -> JNIEnv*;

        }
    }

    // ============================================================================
    // Compile-time String Utilities (C++20 consteval)
    // ============================================================================

    namespace str {

        template<std::size_t I, std::size_t J, std::size_t ...Is, std::size_t ...Js>
        consteval auto operator_add_impl(std::array<char, I> const& lhs, 
                                         std::array<char, J> const& rhs,
                                         std::index_sequence<Is...>, 
                                         std::index_sequence<Js...>) -> std::array<char, I + J - 1> {
            return { lhs[Is]..., rhs[Js]... };
        }

        template<std::size_t I, std::size_t J>
        consteval auto operator+(std::array<char, I> const& lhs, 
                                 std::array<char, J> const& rhs) -> std::array<char, I + J - 1> {
            return operator_add_impl(lhs, rhs, 
                                     std::make_index_sequence<I - 1>{}, 
                                     std::make_index_sequence<J>{});
        }

        template<std::size_t N, std::size_t ...Ns>
        consteval auto arrayify_impl(const char (&s)[N], std::index_sequence<Ns...>) -> std::array<char, N> {
            return {s[Ns]...};
        }

        template<std::size_t N>
        consteval auto arrayify(const char (&s)[N]) -> std::array<char, N> {
            return arrayify_impl<N>(s, std::make_index_sequence<N>{});
        }

        template<std::size_t N, std::size_t C>
        consteval auto find_next(const std::array<char, N>& s, 
                                 const std::array<char, C>& str, 
                                 std::size_t start_pos = 0) -> std::optional<std::size_t> {
            if constexpr (C == 0 || C > N) {
                return std::nullopt;
            }
            for (; start_pos <= N - C; ++start_pos) {
                std::size_t j = 0;
                for (; j < C; ++j) {
                    if (s[start_pos + j] != str[j])
                        break;
                }
                if (j == C)
                    return start_pos;
            }
            return std::nullopt;
        }

        template<std::size_t D, std::size_t N>
        consteval auto shrink_array(std::array<char, N> const& s) -> std::array<char, N - D> {
            std::array<char, N - D> d{};
            for (std::size_t i = 0; i < N - D; ++i) {
                d[i] = s[i];
            }
            return d;
        }

        template<std::size_t T, std::size_t N, std::size_t ...Ns>
        consteval auto expand_impl(const char (&s)[N], std::index_sequence<Ns...>) -> std::array<char, N + T> {
            return std::array<char, N + T>{ s[Ns]..., '\0' };
        }

        template<std::size_t T, std::size_t N>
        consteval auto expand(const char (&s)[N]) -> std::array<char, N + T> {
            return expand_impl<T>(s, std::make_index_sequence<N>{});
        }

        template<typename ...Arrays>
        consteval auto add_all(Arrays const& ...arrays) {
            return (arrays + ...);
        }

    } // namespace str

    // ============================================================================
    // Detail Implementation - String Processing
    // ============================================================================

    namespace detail {

        using namespace str;

        template<std::size_t N>
        consteval auto arraysize_of([[maybe_unused]] std::array<char, N> const& arr) -> std::size_t {
            return N;
        }

        template<std::size_t N>
        consteval auto namespace_depth(std::array<char, N> const& path) -> int {
            int depth = 0;
            for (std::size_t i = 0; path[i] != '\0' && i + 1 < N; ++i) {
                if (path[i] == ':' && path[i + 1] == ':') {
                    ++depth;
                }
            }
            return depth;
        }

        template<std::size_t M, std::size_t N>
        consteval void replace_scope_to_slash(std::array<char, M>& signature, 
                                              std::array<char, N> const& symbol) {
            constexpr std::array<char, 2> scope_opt = {':', ':'};

            std::size_t start_pos = 0, prev_pos = 0, write_pos = 0;

            while (auto maybe_pos = find_next(symbol, scope_opt, start_pos)) {
                std::size_t pos = maybe_pos.value();

                if (pos < prev_pos)
                    break;

                for (std::size_t i = prev_pos; i < pos; ++i) {
                    signature[write_pos++] = symbol[i];
                }

                signature[write_pos++] = '/';

                prev_pos = pos + scope_opt.size();
                start_pos = prev_pos;
            }

            for (std::size_t i = prev_pos; i < arraysize_of(symbol); ++i) {
                signature[write_pos++] = symbol[i];
            }
        }

        template<std::size_t N>
        consteval auto namespace_to_signature(std::array<char, N> const& arr) -> std::array<char, N> {
            std::array<char, N> signature{};
            signature.fill('\0');
            constexpr std::array<char, 2> window = {':', ':'};

            std::size_t start_pos = 0;
            std::size_t prev_pos = 0;
            std::size_t write_pos = 0;

            while (auto maybe_pos = find_next(arr, window, start_pos)) {
                std::size_t pos = maybe_pos.value();

                if (pos < prev_pos) {
                    break;
                }

                for (std::size_t i = prev_pos; i < pos; ++i) {
                    signature[write_pos++] = arr[i];
                }

                signature[write_pos++] = '/';

                prev_pos = pos + window.size();
                start_pos = prev_pos;
            }

            for (std::size_t i = prev_pos; i < N; ++i) {
                signature[write_pos++] = arr[i];
            }

            return signature;
        }

        template<std::size_t N>
        consteval auto namespace_to_signature(const char (&s)[N]) -> std::array<char, N> {
            return namespace_to_signature(arrayify(s));
        }

        template<bool IsArray, std::size_t N>
        consteval auto to_fqcn(std::array<char, N> const& n) {
            constexpr auto arr_prefix = arrayify("[");
            constexpr auto prefix = arrayify("L");
            constexpr auto suffix = arrayify(";");
            if constexpr (IsArray) {
                return add_all(arr_prefix, prefix, n, suffix);
            } else {
                return add_all(prefix, n, suffix);
            }
        }

        template<bool IsArray, std::size_t N>
        consteval auto to_fqcn(const char (&n)[N]) {
            return to_fqcn<IsArray>(arrayify(n));
        }

    } // namespace detail

    // ============================================================================
    // Class Signature Override System (Cyclic Reference Resolution)
    // ============================================================================

    namespace traits {

        // Primary template: no override
        template<typename T>
        struct class_signature_override {
            static constexpr bool has_override = false;
        };

        // Concept: has override specialization
        template<typename T>
        concept HasClassSignatureOverride = requires {
            { class_signature_override<T>::value } -> std::convertible_to<std::span<const char>>;
        };

        // Concept: has CLASS_SIGNATURE member
        template<typename T>
        concept HasClassSignatureMember = requires {
            { T::CLASS_SIGNATURE } -> std::convertible_to<std::span<const char>>;
        };

        // Concept: has any class signature (override or member)
        template<typename T>
        concept HasClassSignature = HasClassSignatureOverride<T> || HasClassSignatureMember<T>;

        // Get class signature (prioritize override)
        template<typename T>
            requires HasClassSignature<T>
        consteval auto class_signature() {
            if constexpr (HasClassSignatureOverride<T>) {
                return class_signature_override<T>::value;
            } else {
                return T::CLASS_SIGNATURE;
            }
        }

        // Convenient variable template
        template<typename T>
            requires HasClassSignature<T>
        inline constexpr auto class_signature_v = class_signature<T>();

    } // namespace traits

    // ============================================================================
    // Concepts (C++20)
    // ============================================================================

    namespace concepts {

        // JNI primitive types
        template<typename T>
        concept JniPrimitive = std::same_as<T, jboolean> || std::same_as<T, jbyte> ||
                               std::same_as<T, jchar> || std::same_as<T, jshort> ||
                               std::same_as<T, jint> || std::same_as<T, jlong> ||
                               std::same_as<T, jfloat> || std::same_as<T, jdouble> ||
                               std::same_as<T, bool> || std::same_as<T, char>;

        // JNI object types (derived from _jobject)
        // [BUG FIX] Correct direction: _jobject is base of T
        template<typename T>
        concept JniObjectType = std::is_pointer_v<T> && 
                                std::is_base_of_v<_jobject, std::remove_pointer_t<T>>;

        // JNI array types
        template<typename T>
        concept JniArrayType = std::same_as<T, jbooleanArray> || std::same_as<T, jbyteArray> ||
                               std::same_as<T, jcharArray> || std::same_as<T, jshortArray> ||
                               std::same_as<T, jintArray> || std::same_as<T, jlongArray> ||
                               std::same_as<T, jfloatArray> || std::same_as<T, jdoubleArray> ||
                               std::same_as<T, jobjectArray>;

        // Mirror class: has class signature (via override or member)
        template<typename T>
        concept MirrorClass = traits::HasClassSignature<T>;

        // Complete mirror class (can be instantiated)
        template<typename T>
        concept CompleteMirrorClass = MirrorClass<T> && requires { sizeof(T); };

        // Derived from BaseClass (complete types only)
        template<typename T>
        concept DerivedFromJBase = CompleteMirrorClass<T> && 
                                   std::is_base_of_v<detail::BaseClass<T>, T>;

        // String-like types
        template<typename T>
        concept StringLike = std::same_as<std::remove_cvref_t<T>, std::string> ||
                             std::same_as<std::remove_cvref_t<T>, std::string_view>;

        // Char array reference
        template<typename T>
        concept CharArrayRef = std::is_array_v<std::remove_reference_t<T>> &&
                               std::same_as<std::remove_extent_t<std::remove_reference_t<T>>, const char>;

    } // namespace concepts

    // ============================================================================
    // ObjectRef: Lazy reference for incomplete mirror types (Cyclic Reference)
    // ============================================================================

    template<typename Mirror>
    class ObjectRef {
    public:
        constexpr ObjectRef() noexcept = default;
        
        explicit ObjectRef(jobject obj) noexcept : obj_(obj) {}

        // Implicit conversion to jobject (always available)
        [[nodiscard]] operator jobject() const noexcept { 
            return obj_; 
        }

        // Implicit conversion to Mirror (only when Mirror is complete)
        template<typename M = Mirror>
            requires requires { sizeof(M); }
        [[nodiscard]] operator M() const {
            return M{obj_};
        }

        // Explicit get as jobject
        [[nodiscard]] auto get() const noexcept -> jobject {
            return obj_;
        }

        // Check if valid
        [[nodiscard]] explicit operator bool() const noexcept {
            return obj_ != nullptr;
        }

    private:
        jobject obj_ = nullptr;
    };

    // ============================================================================
    // Type Aliases
    // ============================================================================

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

    // ============================================================================
    // Type Traits
    // ============================================================================

    namespace traits {

        template<class>
        struct deferred_false : std::false_type {};

        // template<typename Type>
        // class typeof;

        // Check if type is derived from BaseClass (uses concept)
        template<class Class>
        inline constexpr bool is_derived_from_jbase = concepts::DerivedFromJBase<Class>;

        // Check if type is a mirror class (has signature)
        template<class Class>
        inline constexpr bool is_mirror_class_v = concepts::MirrorClass<Class>;

        // Check if type is a JNI object
        template<typename Tp>
        inline constexpr bool is_jni_object_v = concepts::JniObjectType<Tp>;

        // Check if type is a JNI primitive
        template<typename T>
        inline constexpr bool is_jni_primitive_v = concepts::JniPrimitive<T>;

        // Signature traits
        template<typename Type, bool = std::is_array_v<Type>, bool = is_mirror_class_v<std::remove_extent_t<Type>>>
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

        // Array signatures
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

        // String signatures
        template<std::size_t N> struct signature<const char (&)[N], false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<std::size_t N> struct signature<const char (&)[N], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<std::size_t N>
        struct signature<const char[N], false, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<std::size_t N>
        struct signature<const char[N], true, false> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        template<> struct signature<jstring> {
            static constexpr auto SIGNATURE = str::arrayify("Ljava/lang/String;");
        };

        // ========================================================================
        // JNI Field Access Traits (using concepts)
        // ========================================================================

        template<class Type, bool IsStatic>
        struct jni_field;

        // Non-static field for mirror class types
        template<concepts::DerivedFromJBase Type>
        struct jni_field<Type, false> {
            [[nodiscard]] static auto access(JNIEnv* env, jfieldID field_id, jobject receiver) -> Type {
                return Type(env->GetObjectField(receiver, field_id));
            }

            static void set(JNIEnv* env, jfieldID field_id, jobject receiver, jobject value) {
                env->SetObjectField(receiver, field_id, value);
            }
        };

        // Static field for mirror class types
        template<concepts::DerivedFromJBase Type>
        struct jni_field<Type, true> {
            [[nodiscard]] static auto access(JNIEnv* env, jfieldID field_id, jclass cls) -> Type {
                return Type(env->GetStaticObjectField(cls, field_id));
            }

            static void set(JNIEnv* env, jfieldID field_id, jclass cls, jobject value) {
                env->SetStaticObjectField(cls, field_id, value);
            }
        };

        // Primitive field specializations
        #define JBRIDGE_DEFINE_FIELD_TRAIT(jtype, JniGetMethod, JniSetMethod, JniStaticGetMethod, JniStaticSetMethod) \
            template<> struct jni_field<jtype, false> { \
                [[nodiscard]] static auto access(JNIEnv* env, jfieldID field_id, jobject receiver) -> jtype { \
                    return env->JniGetMethod(receiver, field_id); \
                } \
                static void set(JNIEnv* env, jfieldID field_id, jobject receiver, jtype value) { \
                    env->JniSetMethod(receiver, field_id, value); \
                } \
            }; \
            template<> struct jni_field<jtype, true> { \
                [[nodiscard]] static auto access(JNIEnv* env, jfieldID field_id, jclass cls) -> jtype { \
                    return env->JniStaticGetMethod(cls, field_id); \
                } \
                static void set(JNIEnv* env, jfieldID field_id, jclass cls, jtype value) { \
                    env->JniStaticSetMethod(cls, field_id, value); \
                } \
            };

        JBRIDGE_DEFINE_FIELD_TRAIT(jboolean, GetBooleanField, SetBooleanField, GetStaticBooleanField, SetStaticBooleanField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jbyte,    GetByteField,    SetByteField,    GetStaticByteField,    SetStaticByteField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jchar,    GetCharField,    SetCharField,    GetStaticCharField,    SetStaticCharField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jshort,   GetShortField,   SetShortField,   GetStaticShortField,   SetStaticShortField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jint,     GetIntField,     SetIntField,     GetStaticIntField,     SetStaticIntField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jlong,    GetLongField,    SetLongField,    GetStaticLongField,    SetStaticLongField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jfloat,   GetFloatField,   SetFloatField,   GetStaticFloatField,   SetStaticFloatField)
        JBRIDGE_DEFINE_FIELD_TRAIT(jdouble,  GetDoubleField,  SetDoubleField,  GetStaticDoubleField,  SetStaticDoubleField)

        #undef JBRIDGE_DEFINE_FIELD_TRAIT

        // ========================================================================
        // JNI Method Call Traits (using concepts)
        // ========================================================================

        template<typename Type, bool IsStatic>
        struct jni_call;

        #define JBRIDGE_DEFINE_CALL_TRAIT(jtype, JniMethod, JniStaticMethod) \
            template<> struct jni_call<jtype, false> { \
                template<typename ...Args> \
                [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jobject instance, Args&&... args) -> jtype { \
                    return env->JniMethod(instance, method_id, std::forward<Args>(args)...); \
                } \
            }; \
            template<> struct jni_call<jtype, true> { \
                template<typename ...Args> \
                [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jclass cls, Args&&... args) -> jtype { \
                    return env->JniStaticMethod(cls, method_id, std::forward<Args>(args)...); \
                } \
            };

        JBRIDGE_DEFINE_CALL_TRAIT(jboolean, CallBooleanMethod, CallStaticBooleanMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jbyte,    CallByteMethod,    CallStaticByteMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jchar,    CallCharMethod,    CallStaticCharMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jshort,   CallShortMethod,   CallStaticShortMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jint,     CallIntMethod,     CallStaticIntMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jlong,    CallLongMethod,    CallStaticLongMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jfloat,   CallFloatMethod,   CallStaticFloatMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jdouble,  CallDoubleMethod,  CallStaticDoubleMethod)
        JBRIDGE_DEFINE_CALL_TRAIT(jobject,  CallObjectMethod,  CallStaticObjectMethod)

        #undef JBRIDGE_DEFINE_CALL_TRAIT

        // bool delegates to jboolean
        template<> struct jni_call<bool, false> {
            template<typename ...Args>
            [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jobject instance, Args&&... args) -> bool {
                return static_cast<bool>(jni_call<jboolean, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }
        };

        template<> struct jni_call<bool, true> {
            template<typename ...Args>
            [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jclass cls, Args&&... args) -> bool {
                return static_cast<bool>(jni_call<jboolean, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }
        };

        // char delegates to jchar
        template<> struct jni_call<char, false> {
            template<typename ...Args>
            [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jobject instance, Args&&... args) -> char {
                return static_cast<char>(jni_call<jchar, false>::call(env, method_id, instance, std::forward<Args>(args)...));
            }
        };

        template<> struct jni_call<char, true> {
            template<typename ...Args>
            [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jclass cls, Args&&... args) -> char {
                return static_cast<char>(jni_call<jchar, true>::call(env, method_id, cls, std::forward<Args>(args)...));
            }
        };

        // void specialization
        template<> struct jni_call<void, false> {
            template<typename ...Args>
            static void call(JNIEnv* env, jmethodID method_id, jobject instance, Args&&... args) {
                env->CallVoidMethod(instance, method_id, std::forward<Args>(args)...);
            }
        };

        template<> struct jni_call<void, true> {
            template<typename ...Args>
            static void call(JNIEnv* env, jmethodID method_id, jclass cls, Args&&... args) {
                env->CallStaticVoidMethod(cls, method_id, std::forward<Args>(args)...);
            }
        };

        // Array type call specializations
        #define JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(ArrayType) \
            template<> struct jni_call<ArrayType, false> { \
                template<typename ...Args> \
                [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jobject instance, Args&&... args) -> ArrayType { \
                    return static_cast<ArrayType>(jni_call<jobject, false>::call(env, method_id, instance, std::forward<Args>(args)...)); \
                } \
            }; \
            template<> struct jni_call<ArrayType, true> { \
                template<typename ...Args> \
                [[nodiscard]] static auto call(JNIEnv* env, jmethodID method_id, jclass cls, Args&&... args) -> ArrayType { \
                    return static_cast<ArrayType>(jni_call<jobject, true>::call(env, method_id, cls, std::forward<Args>(args)...)); \
                } \
            };

        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jbooleanArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jbyteArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jcharArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jshortArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jintArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jlongArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jfloatArray)
        JBRIDGE_DEFINE_ARRAY_CALL_TRAIT(jdoubleArray)

        #undef JBRIDGE_DEFINE_ARRAY_CALL_TRAIT

        // ========================================================================
        // Primitive Wrapper Traits
        // ========================================================================

        template<typename T>
        struct primitive_wrap;

        #define JBRIDGE_DEFINE_PRIMITIVE_WRAP(type, class_signature) \
            template<> struct primitive_wrap<type> { \
                static constexpr auto WRAPPER_SIGNATURE = str::arrayify(#class_signature); \
            };

        JBRIDGE_DEFINE_PRIMITIVE_WRAP(bool,     java/lang/Boolean)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jboolean, java/lang/Boolean)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jbyte,    java/lang/Byte)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(char,     java/lang/Character)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jchar,    java/lang/Character)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jshort,   java/lang/Short)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jint,     java/lang/Integer)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jlong,    java/lang/Long)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jfloat,   java/lang/Float)
        JBRIDGE_DEFINE_PRIMITIVE_WRAP(jdouble,  java/lang/Double)

        #undef JBRIDGE_DEFINE_PRIMITIVE_WRAP

        // ========================================================================
        // FQCN Generation (uses class_signature_v for cyclic reference support)
        // ========================================================================

        template<typename Type>
        consteval auto fqcnify() {
            if constexpr (is_mirror_class_v<std::remove_extent_t<Type>>) {
                // Use class_signature_v to support forward-declared mirrors
                return detail::to_fqcn<std::is_array_v<Type>>(class_signature_v<std::remove_extent_t<Type>>);
            } else {
                return signature<Type>::SIGNATURE;
            }
        }

        // ========================================================================
        // Array Wrapper Detection
        // ========================================================================

        template<typename T>
        struct is_array_wrapper : std::false_type {};

        template<typename Mirror>
        struct is_array_wrapper<ObjectArray<Mirror>> : std::true_type {};

        template<typename ArrayType, typename ElementType>
        struct is_array_wrapper<detail::JPrimitiveArray<ArrayType, ElementType>> : std::true_type {};

        template<class Class>
        inline constexpr bool is_array_wrapper_v = is_array_wrapper<Class>::value;

        // ========================================================================
        // Type Validation
        // ========================================================================

        template<typename T, bool = is_mirror_class_v<std::remove_extent_t<T>>, bool = std::is_array_v<T>>
        struct type_validfy {
            using type = T;
        };

        // Non-array mirror class -> jobject
        template<typename T>
        struct type_validfy<T, true, false> {
            using type = jobject;
        };

        // Primitive array type mappings
        template<> struct type_validfy<jboolean[], false, true> { using type = jbooleanArray; };
        template<> struct type_validfy<bool[], false, true>     { using type = jbooleanArray; };
        template<> struct type_validfy<jbyte[], false, true>    { using type = jbyteArray; };
        template<> struct type_validfy<jchar[], false, true>    { using type = jcharArray; };
        template<> struct type_validfy<char[], false, true>     { using type = jcharArray; };
        template<> struct type_validfy<short[], false, true>    { using type = jshortArray; };
        template<> struct type_validfy<jint[], false, true>     { using type = jintArray; };
        template<> struct type_validfy<jlong[], false, true>    { using type = jlongArray; };
        template<> struct type_validfy<jfloat[], false, true>   { using type = jfloatArray; };
        template<> struct type_validfy<jdouble[], false, true>  { using type = jdoubleArray; };

        // Mirror array -> ArrayType (wrapper), then double type_validfy resolves to jobject
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

        // ========================================================================
        // Array Wrapper Type Mapping
        // ========================================================================

        template<typename T, bool = is_mirror_class_v<std::remove_extent_t<T>>, bool = std::is_array_v<T>>
        struct array_wrapper {
            using type = T;
        };

        template<typename T>
        struct array_wrapper<T, true, true> {
            using type = ObjectArray<std::remove_extent_t<T>>;
        };

        template<> struct array_wrapper<void>        { using type = void; };
        template<> struct array_wrapper<jboolean[]>  { using type = BooleanArray; };
        template<> struct array_wrapper<bool[]>      { using type = BooleanArray; };
        template<> struct array_wrapper<jbyte[]>     { using type = ByteArray; };
        template<> struct array_wrapper<jchar[]>     { using type = CharArray; };
        template<> struct array_wrapper<char[]>      { using type = CharArray; };
        template<> struct array_wrapper<short[]>     { using type = ShortArray; };
        template<> struct array_wrapper<jint[]>      { using type = IntArray; };
        template<> struct array_wrapper<jlong[]>     { using type = LongArray; };
        template<> struct array_wrapper<jfloat[]>    { using type = FloatArray; };
        template<> struct array_wrapper<jdouble[]>   { using type = DoubleArray; };

        template<typename T>
        using array_wrapper_t = typename array_wrapper<T>::type;

        // ========================================================================
        // Method Return Type (supports incomplete mirror types via ObjectRef)
        // ========================================================================

        template<typename T>
        struct method_return {
            using type = array_wrapper_t<T>;
        };

        // For incomplete mirror class return types, use ObjectRef
        template<typename T>
            requires (is_mirror_class_v<T> && !std::is_array_v<T>)
        struct method_return<T> {
            // Check if T is complete
            static constexpr bool is_complete = requires { sizeof(T); };
            
            // Use ObjectRef for potentially incomplete types
            using type = std::conditional_t<is_complete, T, ObjectRef<T>>;
        };

        template<typename T>
        using method_return_t = typename method_return<T>::type;

        // ========================================================================
        // Const Char Array Reference Detection
        // ========================================================================

        template<typename T>
        struct is_const_chars_ref : std::false_type {};

        template<std::size_t N> 
        struct is_const_chars_ref<const char (&)[N]> : std::true_type {};

        template<typename T>
        inline constexpr bool is_const_chars_ref_v = is_const_chars_ref<T>::value;

    } // namespace traits

    // ============================================================================
    // Signature Tokenizer
    // ============================================================================

    namespace tokenizer {

        using namespace traits;
        using namespace detail;

        template<typename ...Types>
        consteval auto build_param_signature() {
            return str::add_all(str::arrayify("("), traits::fqcnify<Types>()..., str::arrayify(")"));
        }

        template<typename ReturnType>
        consteval auto build_return_signature() {
            return traits::fqcnify<ReturnType>();
        }

        template<typename ReturnType, typename ...Types>
        consteval auto build_function_signature() {
            return str::add_all(build_param_signature<Types...>(), build_return_signature<ReturnType>());
        }

    } // namespace tokenizer

    // ============================================================================
    // JNI Core Implementation
    // ============================================================================

    namespace detail {

        inline constexpr bool Static = true;
        inline constexpr bool NonStatic = false;

        namespace jni {

            inline static JavaVM* vm_ = nullptr;

            [[nodiscard]] inline auto GetEnv() noexcept -> JNIEnv* {
                struct Attacher {
                    explicit Attacher() {
                        vm_->AttachCurrentThread(reinterpret_cast<void**>(&env_), nullptr);
                    }

                    ~Attacher() {
                        vm_->DetachCurrentThread();
                    }

                    JNIEnv* env_{};
                };

                thread_local Attacher attacher;
                return attacher.env_;
            }

            [[nodiscard]] inline auto GetDefaultConstructor(JNIEnv* env, jclass cls) -> jmethodID {
                return env->GetMethodID(cls, "<init>", "()V");
            }

            template<typename RefType>
            [[nodiscard]] inline auto MakeGlobalRef(JNIEnv* env, jobject ref) -> RefType {
                return static_cast<RefType>(env->NewGlobalRef(ref));
            }

            template<typename RefType>
            inline void DeleteGlobalRef(JNIEnv* env, RefType ref) {
                if (env->GetObjectRefType(ref) == JNIGlobalRefType) {
                    env->DeleteGlobalRef(ref);
                }
            }

            inline void DeleteGlobalRefWithoutJNIEnv(jobject ref) {
                DeleteGlobalRef(GetEnv(), ref);
            }

            // FindClass using class_signature_v (supports forward-declared mirrors)
            template<std::size_t N>
            [[nodiscard]] inline auto FindClass(std::array<char, N> const& class_signature) -> jclass {
                return GetEnv()->FindClass(class_signature.data());
            }

            // FindClass for mirror types using trait
            template<concepts::MirrorClass Mirror>
            [[nodiscard]] inline auto FindClassFor() -> jclass {
                return GetEnv()->FindClass(traits::class_signature_v<Mirror>.data());
            }

            // Validfy: Convert C++ types to JNI-compatible types
            template<typename T>
            [[nodiscard]] inline auto Validfy(T&& t) {
                using ArgType = std::remove_cvref_t<T>;

                if constexpr (concepts::JniPrimitive<ArgType>) {
                    return t;
                } else if constexpr (concepts::JniObjectType<ArgType>) {
                    return t;
                } else if constexpr (concepts::DerivedFromJBase<ArgType>) {
                    return t.GetObject();
                } else if constexpr (concepts::StringLike<ArgType>) {
                    return GetEnv()->NewStringUTF(t.data());
                } else if constexpr (traits::is_const_chars_ref_v<T>) {
                    return GetEnv()->NewStringUTF(t);
                } else if constexpr (traits::is_array_wrapper_v<ArgType>) {
                    return t.Raw();
                } else if constexpr (std::is_same_v<ArgType, ObjectRef<typename ArgType::value_type>> || 
                                     requires { typename ArgType::value_type; static_cast<jobject>(t); }) {
                    // ObjectRef support
                    return static_cast<jobject>(t);
                } else {
                    return t;
                }
            }

            // JObjectify: Convert to boxed Java object
            template<typename T>
            [[nodiscard]] inline auto JObjectify(T&& t) -> jobject {
                if constexpr (concepts::JniPrimitive<T>) {
                    using TWrapped = traits::primitive_wrap<T>;
                    auto env = GetEnv();
                    auto cls = env->FindClass(TWrapped::WRAPPER_SIGNATURE.data());
                    static auto methodid_value = env->GetStaticMethodID(
                        cls, "valueOf",
                        str::add_all(
                            str::arrayify("("),
                            traits::signature<T>::SIGNATURE,
                            str::arrayify(")"),
                            to_fqcn<false>(TWrapped::WRAPPER_SIGNATURE)
                        ).data()
                    );
                    return env->CallStaticObjectMethod(cls, methodid_value, t);
                } else if constexpr (concepts::DerivedFromJBase<T>) {
                    return t.GetObject();
                } else if constexpr (concepts::JniObjectType<T>) {
                    return t;
                } else {
                    static_assert(traits::deferred_false<T>::value, "Cannot convert type to jobject");
                }
            }

            // ====================================================================
            // Primitive Array Operations
            // ====================================================================

            template<typename ElementType>
            [[nodiscard]] auto NewPrimitiveArray(JNIEnv* env, std::size_t length) {
                auto jsize_len = static_cast<jsize>(length);

                if constexpr (std::same_as<ElementType, jboolean>) {
                    return env->NewBooleanArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jbyte>) {
                    return env->NewByteArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jchar>) {
                    return env->NewCharArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jshort>) {
                    return env->NewShortArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jint>) {
                    return env->NewIntArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jlong>) {
                    return env->NewLongArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jfloat>) {
                    return env->NewFloatArray(jsize_len);
                } else if constexpr (std::same_as<ElementType, jdouble>) {
                    return env->NewDoubleArray(jsize_len);
                } else {
                    static_assert(traits::deferred_false<ElementType>::value, "Unsupported JNI array element type");
                }
            }

            template<concepts::JniArrayType JArrayType>
            [[nodiscard]] auto GetArrayElements(JNIEnv* env, JArrayType array) {
                if constexpr (std::same_as<JArrayType, jbooleanArray>) {
                    return env->GetBooleanArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jbyteArray>) {
                    return env->GetByteArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jcharArray>) {
                    return env->GetCharArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jshortArray>) {
                    return env->GetShortArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jintArray>) {
                    return env->GetIntArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jlongArray>) {
                    return env->GetLongArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jfloatArray>) {
                    return env->GetFloatArrayElements(array, nullptr);
                } else if constexpr (std::same_as<JArrayType, jdoubleArray>) {
                    return env->GetDoubleArrayElements(array, nullptr);
                } else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI array type");
                }
            }

            template<concepts::JniArrayType JArrayType, typename ElementType>
            void ReleaseArrayRegion(JNIEnv* env, JArrayType array, ElementType* elements) {
                if constexpr (std::same_as<JArrayType, jbooleanArray>) {
                    env->ReleaseBooleanArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jbyteArray>) {
                    env->ReleaseByteArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jcharArray>) {
                    env->ReleaseCharArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jshortArray>) {
                    env->ReleaseShortArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jintArray>) {
                    env->ReleaseIntArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jlongArray>) {
                    env->ReleaseLongArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jfloatArray>) {
                    env->ReleaseFloatArrayElements(array, elements, 0);
                } else if constexpr (std::same_as<JArrayType, jdoubleArray>) {
                    env->ReleaseDoubleArrayElements(array, elements, 0);
                } else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI array type");
                }
            }

            template<concepts::JniArrayType JArrayType, typename ElementType>
            void SetArrayRegion(JNIEnv* env, JArrayType& array, jsize start, jsize len, const ElementType* buf) {
                if constexpr (std::same_as<JArrayType, jbooleanArray>) {
                    env->SetBooleanArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jbyteArray>) {
                    env->SetByteArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jcharArray>) {
                    env->SetCharArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jshortArray>) {
                    env->SetShortArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jintArray>) {
                    env->SetIntArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jlongArray>) {
                    env->SetLongArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jfloatArray>) {
                    env->SetFloatArrayRegion(array, start, len, buf);
                } else if constexpr (std::same_as<JArrayType, jdoubleArray>) {
                    env->SetDoubleArrayRegion(array, start, len, buf);
                } else {
                    static_assert(traits::deferred_false<JArrayType>::value, "Unsupported JNI type");
                }
            }

            // ====================================================================
            // JNI Reference Encoding (for tagged pointers)
            // ====================================================================

            inline constexpr auto kJniTag = static_cast<std::uintptr_t>(0xECD8000000000000ULL);

            [[nodiscard]] constexpr auto IsEncoded(std::uintptr_t reference) noexcept -> bool {
                return (reference & kJniTag) == kJniTag;
            }

            template<typename Tp>
            [[nodiscard]] constexpr auto Encode(Tp reference) noexcept -> std::uintptr_t {
                return reinterpret_cast<std::uintptr_t>(reference) | kJniTag;
            }

            template<typename Tp>
            [[nodiscard]] constexpr auto Decode(std::uintptr_t encoded) noexcept -> Tp {
                return reinterpret_cast<Tp>(encoded & ~kJniTag);
            }

            // ====================================================================
            // JniObject: Tagged pointer wrapper for JNI references
            // ====================================================================

            template<typename Tp>
            class JniObject {
            public:
                constexpr JniObject() noexcept : reference_(0) {}

                explicit JniObject(Tp ptr) noexcept 
                    : reference_(Encode(ptr)) {}

                JniObject(JniObject const& o) noexcept = default;

                JniObject& operator=(JniObject const& o) noexcept = default;

                JniObject(JniObject&& o) noexcept = default;

                JniObject& operator=(JniObject&& o) noexcept = default;

                [[nodiscard]] auto Get() const noexcept -> Tp {
                    return Decode<Tp>(reference_);
                }

                void Set(Tp ptr) noexcept {
                    reference_ = Encode(ptr);
                }

                [[nodiscard]] explicit operator bool() const noexcept {
                    return reference_ != 0 && Decode<Tp>(reference_) != nullptr;
                }

            private:
                std::uintptr_t reference_;
            };

            // ====================================================================
            // JniRef: Automatic global/local reference management
            // ====================================================================

            template<typename Tp>
            class JniRef {
                static constexpr std::size_t FieldSize = sizeof(Tp);
                static constexpr std::size_t PointerSize = sizeof(void*);

            private:
                static void SearchJniObjectOnField(void* base, 
                                                   std::function<void(JNIEnv*, std::uintptr_t&)> const& callback) {
                    auto env = GetEnv();
                    auto start = reinterpret_cast<std::uintptr_t>(base);
                    for (std::size_t off = 0; off + PointerSize <= FieldSize; off += PointerSize) {
                        auto& value = *reinterpret_cast<std::uintptr_t*>(start + off);
                        if (IsEncoded(value)) {
                            callback(env, value);
                        }
                    }
                }

            public:
                static void Promote(Tp* base) {
                    SearchJniObjectOnField(base, [](JNIEnv* env, std::uintptr_t& value) {
                        if (IsEncoded(value)) {
                            value = Encode(env->NewGlobalRef(Decode<jobject>(value)));
                        }
                    });
                }

                static void Demote(Tp* base) {
                    SearchJniObjectOnField(base, [](JNIEnv* env, std::uintptr_t const& value) {
                        if (IsEncoded(value)) {
                            env->DeleteGlobalRef(Decode<jobject>(value));
                        }
                    });
                }
            };

        } // namespace jni

        // ========================================================================
        // BaseClass: CRTP base for mirror classes
        // ========================================================================

        template<typename MirrorType>
        class BaseClass {
        public:
            using ArrayType = ObjectArray<MirrorType>;

            explicit BaseClass() noexcept 
                : env_(jni::GetEnv())
                , declaring_class_(env_->FindClass(traits::class_signature_v<MirrorType>.data()))
                , object_([this] {
                    static jmethodID default_constructor = 
                        env_->GetMethodID(declaring_class_.Get(), "<init>", "()V");
                    return env_->NewObject(declaring_class_.Get(), default_constructor);
                }()) 
            {}

            explicit BaseClass(jobject instance) noexcept 
                : env_(jni::GetEnv())
                , declaring_class_(env_->FindClass(traits::class_signature_v<MirrorType>.data()))
                , object_(instance) 
            {}

            BaseClass(BaseClass const& o) noexcept = default;
            BaseClass(BaseClass&& o) noexcept = default;
            BaseClass& operator=(BaseClass const& o) noexcept = default;
            BaseClass& operator=(BaseClass&& o) noexcept = default;

            [[nodiscard]] auto GetObject() -> jobject {
                return object_.Get();
            }

            [[nodiscard]] auto GetDeclaringClass() -> jclass {
                return declaring_class_.Get();
            }

        protected:
            JNIEnv* env_{};
            jni::JniObject<jclass> declaring_class_;
            jni::JniObject<jobject> object_;
        };

        // ========================================================================
        // Constructor: JNI constructor wrapper
        // ========================================================================

        class Constructor {
        public:
            explicit Constructor(jmethodID declaring_ctor) noexcept 
                : declaring_ctor_(declaring_ctor) {}

            template<typename ...Args>
            [[nodiscard]] auto call(jclass cls, Args&&... args) -> jobject {
                return jni::GetEnv()->NewObject(
                    cls, declaring_ctor_, 
                    jni::Validfy(std::forward<Args>(args))...
                );
            }

        private:
            jmethodID declaring_ctor_;
        };

        // ========================================================================
        // Method: JNI method wrapper
        // ========================================================================

        template<typename ReturnType>
        class Method {
        public:
            explicit Method(jmethodID declaring_method) noexcept 
                : declaring_method_(declaring_method) {}

            template<bool IsStatic, typename ...Args>
                requires IsStatic
            [[nodiscard]] auto call(jclass cls, Args&&... args) 
                -> traits::type_validfy_t<ReturnType> 
            {
                if constexpr (std::is_void_v<traits::type_validfy_t<ReturnType>>) {
                    traits::jni_call<traits::type_validfy_t<ReturnType>, true>::call(
                        jni::GetEnv(), declaring_method_, cls, 
                        jni::Validfy(std::forward<Args>(args))...
                    );
                } else {
                    // Double type_validfy_t: Mirror[] -> ArrayType -> jobject
                    return traits::jni_call<
                        traits::type_validfy_t<traits::type_validfy_t<ReturnType>>, true
                    >::call(
                        jni::GetEnv(), declaring_method_, cls, 
                        jni::Validfy(std::forward<Args>(args))...
                    );
                }
            }

            template<bool IsStatic, typename ...Args>
                requires (!IsStatic)
            [[nodiscard]] auto call(jobject object, Args&&... args) 
                -> traits::type_validfy_t<ReturnType> 
            {
                if constexpr (std::is_void_v<traits::type_validfy_t<ReturnType>>) {
                    traits::jni_call<traits::type_validfy_t<ReturnType>, false>::call(
                        jni::GetEnv(), declaring_method_, object, 
                        jni::Validfy(std::forward<Args>(args))...
                    );
                } else {
                    // Double type_validfy_t: Mirror[] -> ArrayType -> jobject
                    return traits::jni_call<
                        traits::type_validfy_t<traits::type_validfy_t<ReturnType>>, false
                    >::call(
                        jni::GetEnv(), declaring_method_, object, 
                        jni::Validfy(std::forward<Args>(args))...
                    );
                }
            }

        private:
            jmethodID declaring_method_{};
        };

        // ========================================================================
        // Factory Functions
        // ========================================================================

        template<typename ...ArgsTypes, std::size_t N>
        [[nodiscard]] static auto CreateConstructor(
            std::array<char, N> const& class_signature,
            JNIEnv* env = jni::GetEnv()
        ) -> Constructor {
            return Constructor{
                env->GetMethodID(
                    env->FindClass(class_signature.data()),
                    "<init>",
                    tokenizer::build_function_signature<void, std::remove_reference_t<ArgsTypes>...>().data()
                )
            };
        }

        template<bool IsStatic, typename ReturnType, typename ...ParameterTypes, std::size_t N>
        [[nodiscard]] static auto CreateMethod(
            std::array<char, N> const& class_signature,
            std::string_view name,
            JNIEnv* env = jni::GetEnv()
        ) -> Method<ReturnType> {
            if constexpr (IsStatic) {
                return Method<ReturnType>{
                    env->GetStaticMethodID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        tokenizer::build_function_signature<ReturnType, ParameterTypes...>().data()
                    )
                };
            } else {
                return Method<ReturnType>{
                    env->GetMethodID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        tokenizer::build_function_signature<ReturnType, ParameterTypes...>().data()
                    )
                };
            }
        }

        // ========================================================================
        // Field: JNI field wrapper
        // ========================================================================

        template<bool IsStatic, typename FieldType>
        class Field;

        template<typename FieldType>
        class Field<Static, FieldType> {
        public:
            explicit Field(jfieldID field_id) noexcept : declaring_field_(field_id) {}

            Field(Field const&) = default;
            Field& operator=(Field const&) = delete;
            Field(Field&&) = default;
            Field& operator=(Field&&) = delete;

            Field& operator=(FieldType value) {
                Set(value);
                return *this;
            }

            operator FieldType() noexcept {
                return Get();
            }

            [[nodiscard]] auto Update(jclass ref) -> Field {
                class_ref_ = ref;
                return std::move(*this);
            }

            [[nodiscard]] auto Get() -> FieldType {
                return traits::jni_field<FieldType, Static>::access(jni::GetEnv(), declaring_field_, class_ref_);
            }

            void Set(FieldType value) {
                traits::jni_field<FieldType, Static>::set(jni::GetEnv(), declaring_field_, class_ref_, value);
            }

            [[nodiscard]] auto operator()() -> FieldType {
                return Get();
            }

        private:
            jfieldID declaring_field_{};
            jclass class_ref_{};
        };

        template<typename FieldType>
        class Field<NonStatic, FieldType> {
        public:
            explicit Field(jfieldID field_id) noexcept : declaring_field_(field_id) {}

            Field(Field const&) = default;
            Field& operator=(Field const&) = delete;
            Field(Field&&) = default;
            Field& operator=(Field&&) = delete;

            Field& operator=(FieldType value) {
                Set(value);
                return *this;
            }

            operator FieldType() noexcept {
                return Get();
            }

            [[nodiscard]] auto Update(jobject ref) -> Field& {
                object_ref_ = ref;
                return *this;
            }

            [[nodiscard]] auto Get() const -> FieldType {
                return traits::jni_field<FieldType, NonStatic>::access(jni::GetEnv(), declaring_field_, object_ref_);
            }

            void Set(FieldType val) const {
                traits::jni_field<FieldType, NonStatic>::set(jni::GetEnv(), declaring_field_, object_ref_, val);
            }

            [[nodiscard]] auto operator()() -> FieldType {
                return Get();
            }

        private:
            jfieldID declaring_field_{};
            jobject object_ref_{};
        };

        template<bool IsStatic, typename Type, std::size_t N>
        [[nodiscard]] static auto CreateField(
            std::array<char, N> const& class_signature, 
            std::string_view name, 
            JNIEnv* env = jni::GetEnv()
        ) -> Field<IsStatic, Type> {
            if constexpr (IsStatic) {
                return Field<IsStatic, Type>{
                    env->GetStaticFieldID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        traits::fqcnify<Type>().data()
                    )
                };
            } else {
                return Field<IsStatic, Type>{
                    env->GetFieldID(
                        env->FindClass(class_signature.data()),
                        name.data(),
                        traits::fqcnify<Type>().data()
                    )
                };
            }
        }

        // ========================================================================
        // JPrimitiveArray: Wrapper for JNI primitive arrays
        // ========================================================================

        template<typename JArrayType, typename ElementType>
        class JPrimitiveArray {
        public:
            JPrimitiveArray() = default;

            explicit JPrimitiveArray(std::size_t initial_capacity) 
                : env_(jni::GetEnv())
                , array_(jni::NewPrimitiveArray<ElementType>(env_, initial_capacity))
                , elements_(jni::GetArrayElements(env_, array_.Get()))
                , size_(static_cast<std::size_t>(env_->GetArrayLength(array_.Get()))) 
            {}

            explicit JPrimitiveArray(JArrayType array) 
                : env_(jni::GetEnv())
                , array_(array)
                , elements_(jni::GetArrayElements(env_, array))
                , size_(static_cast<std::size_t>(env_->GetArrayLength(array))) 
            {}

            explicit JPrimitiveArray(jobject array) 
                : JPrimitiveArray(static_cast<JArrayType>(array)) {}

            JPrimitiveArray(JPrimitiveArray const& o) 
                : env_(jni::GetEnv())
                , array_(jni::NewPrimitiveArray<ElementType>(env_, o.size_))
                , elements_(jni::GetArrayElements(env_, array_.Get()))
                , size_(o.size_) 
            {
                auto* other_elements = jni::GetArrayElements(env_, o.array_.Get());
                for (std::size_t i = 0; i < size_; ++i) {
                    elements_[i] = other_elements[i];
                }
                jni::ReleaseArrayRegion(env_, o.array_.Get(), other_elements);
            }

            JPrimitiveArray& operator=(JPrimitiveArray const& o) {
                if (this == &o)
                    return *this;

                std::scoped_lock lock(mutex_);

                Release();

                env_ = jni::GetEnv();
                array_ = jni::JniObject<JArrayType>{jni::NewPrimitiveArray<ElementType>(env_, o.size_)};
                size_ = o.size_;
                
                auto* other_elements = jni::GetArrayElements(env_, o.array_.Get());
                elements_ = jni::GetArrayElements(env_, array_.Get());

                for (std::size_t i = 0; i < size_; ++i) {
                    elements_[i] = other_elements[i];
                }

                jni::ReleaseArrayRegion(env_, o.array_.Get(), other_elements);

                return *this;
            }

            JPrimitiveArray(JPrimitiveArray&& o) noexcept 
                : env_(o.env_)
                , array_(o.array_.Get())
                , elements_(o.elements_)
                , size_(o.size_) 
            {
                o.elements_ = nullptr;
                o.size_ = 0;
            }

            JPrimitiveArray& operator=(JPrimitiveArray&& o) noexcept {
                if (this == &o)
                    return *this;

                std::scoped_lock lock(mutex_);

                Release();

                env_ = o.env_;
                elements_ = o.elements_;
                array_ = jni::JniObject<JArrayType>{o.array_.Get()};
                size_ = o.size_;

                o.elements_ = nullptr;
                o.size_ = 0;

                return *this;
            }

            ~JPrimitiveArray() {
                Release();
            }

            [[nodiscard]] auto operator[](std::size_t index) -> ElementType& {
                return elements_[index];
            }

            [[nodiscard]] auto operator[](std::size_t index) const -> ElementType {
                return elements_[index];
            }

            [[nodiscard("Element value should be used")]] 
            auto Get(std::size_t index) -> ElementType {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("JPrimitiveArray::Get(): index out of bounds");

                return elements_[index];
            }

            [[nodiscard("Element value should be used")]] 
            auto Get(std::size_t index) const -> ElementType {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("JPrimitiveArray::Get(): index out of bounds");

                return elements_[index];
            }

            void Set(std::size_t index, ElementType element) {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("JPrimitiveArray::Set(): index out of bounds");

                elements_[index] = element;
            }

            [[nodiscard]] auto Size() const -> std::size_t {
                std::scoped_lock lock(mutex_);
                return size_;
            }

            [[nodiscard]] auto Raw() const -> JArrayType {
                return array_.Get();
            }

            [[nodiscard]] auto begin() -> ElementType* { return elements_; }
            [[nodiscard]] auto end() -> ElementType* { return elements_ + size_; }
            [[nodiscard]] auto begin() const -> const ElementType* { return elements_; }
            [[nodiscard]] auto end() const -> const ElementType* { return elements_ + size_; }
            [[nodiscard]] auto data() -> ElementType* { return elements_; }
            [[nodiscard]] auto data() const -> const ElementType* { return elements_; }
            [[nodiscard]] auto empty() const -> bool { return size_ == 0; }

        private:
            void Release() {
                if (elements_) {
                    jni::ReleaseArrayRegion(env_, array_.Get(), elements_);
                    elements_ = nullptr;
                }
            }

        private:
            JNIEnv* env_ = nullptr;
            jni::JniObject<JArrayType> array_;
            ElementType* elements_ = nullptr;
            std::size_t size_ = 0;
            mutable std::mutex mutex_;
        };

        // ========================================================================
        // JObjectArray: Wrapper for JNI object arrays
        // ========================================================================

        template<typename MirrorClass>
        class JObjectArray {
            static constexpr std::size_t kDefaultCapacity = 10;

        public:
            JObjectArray() : JObjectArray(kDefaultCapacity) {}

            explicit JObjectArray(std::size_t size) 
                : env_(jni::GetEnv())
                , class_(env_->FindClass(traits::class_signature_v<MirrorClass>.data()))
                , array_(env_->NewObjectArray(static_cast<jsize>(size), class_.Get(), nullptr))
                , size_(size) 
            {}

            explicit JObjectArray(jobjectArray array) 
                : env_(jni::GetEnv())
                , class_(env_->FindClass(traits::class_signature_v<MirrorClass>.data()))
                , array_(array)
                , size_(static_cast<std::size_t>(env_->GetArrayLength(array))) 
            {}

            explicit JObjectArray(jobject array) 
                : JObjectArray(static_cast<jobjectArray>(array)) {}

            JObjectArray(JObjectArray const& o) 
                : env_(o.env_)
                , class_(o.class_)
                , array_(o.array_)
                , size_(o.size_) 
            {}

            JObjectArray& operator=(JObjectArray const& o) {
                if (this == &o)
                    return *this;

                std::scoped_lock lock(mutex_);

                env_ = o.env_;
                size_ = o.size_;
                class_ = o.class_;
                array_ = o.array_;

                return *this;
            }

            JObjectArray(JObjectArray&& o) noexcept = default;
            JObjectArray& operator=(JObjectArray&& o) noexcept = default;

            // Returns ObjectRef for incomplete types support
            [[nodiscard]] auto operator[](std::size_t index) -> ObjectRef<MirrorClass> {
                return ObjectRef<MirrorClass>{GetAsRaw(index)};
            }

            [[nodiscard]] auto operator[](std::size_t index) const -> ObjectRef<MirrorClass> {
                return ObjectRef<MirrorClass>{const_cast<JObjectArray*>(this)->GetAsRaw(index)};
            }

            [[nodiscard("Element value should be used")]] 
            auto Get(std::size_t index) -> ObjectRef<MirrorClass> {
                return ObjectRef<MirrorClass>{GetAsRaw(index)};
            }

            [[nodiscard("Raw jobject should be used")]] 
            auto GetAsRaw(std::size_t index) -> jobject {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("JObjectArray::Get(): index out of bounds");

                return env_->GetObjectArrayElement(array_.Get(), static_cast<jsize>(index));
            }

            void Set(std::size_t index, jobject element) {
                std::scoped_lock lock(mutex_);

                if (index >= size_)
                    throw std::out_of_range("JObjectArray::Set(): index out of bounds");

                env_->SetObjectArrayElement(array_.Get(), static_cast<jsize>(index), element);
            }

            // Support ObjectRef as argument
            template<typename T>
                requires std::convertible_to<T, jobject>
            void Set(std::size_t index, T element) {
                Set(index, static_cast<jobject>(element));
            }

            [[nodiscard]] auto Size() const -> std::size_t {
                std::scoped_lock lock(mutex_);
                return size_;
            }

            [[nodiscard]] auto Raw() -> jobjectArray {
                return array_.Get();
            }

            [[nodiscard]] auto empty() const -> bool {
                return size_ == 0;
            }

        private:
            JNIEnv* env_;
            jni::JniObject<jclass> class_;
            jni::JniObject<jobjectArray> array_;
            std::size_t size_;
            mutable std::mutex mutex_;
        };

    } // namespace detail

    // ============================================================================
    // Public API
    // ============================================================================

    template<typename Tp>
        requires (!concepts::JniObjectType<Tp>)
    [[nodiscard]] inline auto MakeGlobalRef(Tp& object) -> std::shared_ptr<Tp> {
        using namespace detail::jni;
        JniRef<Tp>::Promote(&object);
        return {&object, JniRef<Tp>::Demote};
    }

    template<concepts::JniObjectType Op>
    [[nodiscard]] inline auto MakeGlobalRef(Op object) -> std::shared_ptr<std::remove_pointer_t<Op>> {
        using namespace detail::jni;
        return {MakeGlobalRef<Op>(GetEnv(), object), DeleteGlobalRefWithoutJNIEnv};
    }

    inline void Init(JavaVM* vm) noexcept {
        detail::jni::vm_ = vm;
    }

} // namespace jb

// ============================================================================
// Macros - Forward Declaration Support (Cyclic Reference Resolution)
// ============================================================================

// Generate class signature from package::class_name
#define JBRIDGE_INTERNAL_MAKE_SIGNATURE(package, class_name) \
    [] { \
        using namespace jb::detail; \
        auto symbol = arrayify(#package "::" #class_name); \
        constexpr auto symbol_size = arraysize_of(symbol); \
        std::array<char, symbol_size - namespace_depth(arrayify(#package "::" #class_name))> signature{}; \
        replace_scope_to_slash(signature, symbol); \
        return signature; \
    }()

// Forward declare a mirror class (enables cyclic references)
// Usage: JBRIDGE_DECLARE_CLASS(java::lang::reflect, Method)
#define JBRIDGE_DECLARE_CLASS(package, class_name) \
    namespace package { struct class_name; } \
    template<> \
    struct jb::traits::class_signature_override<package::class_name> { \
        static constexpr auto value = JBRIDGE_INTERNAL_MAKE_SIGNATURE(package, class_name); \
    };

// ============================================================================
// Macros - Class Definition (Interface preserved for compatibility)
// ============================================================================

#define JBRIDGE_DEFINE_CLASS(package, class_name, class_scope)                                                      \
namespace package {                                                                                                 \
                                                                                                                    \
    template<typename D>                                                                                            \
    struct _M_base_ ## class_name {                                                                                 \
                                                                                                                    \
        static constexpr auto CLASS_SIGNATURE = JBRIDGE_INTERNAL_MAKE_SIGNATURE(package, class_name);               \
                                                                                                                    \
        template<typename ...Args>                                                                                  \
        [[nodiscard]] static auto new_(Args&&... args) -> D {                                                       \
            static auto constructor = jb::detail::CreateConstructor<Args...>(CLASS_SIGNATURE);                      \
            return D{constructor.call(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...)};   \
        }                                                                                                           \
    };                                                                                                              \
                                                                                                                    \
    struct class_name : public jb::detail::BaseClass<class_name>, public _M_base_ ## class_name<class_name> class_scope; \
}

#define JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(class_name) using jb::detail::BaseClass<class_name>::BaseClass;

// ============================================================================
// Macros - Method Definition (supports cyclic references via method_return_t)
// ============================================================================

#define JBRIDGE_DEFINE_METHOD(return_type, name, ...)                                                               \
template<typename ...Args>                                                                                          \
auto name(Args&&... args) {                                                                                         \
    static auto name ## _ = jb::detail::CreateMethod<false, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name); \
    if constexpr (std::is_void_v<return_type>) {                                                                    \
        name ## _.template call<false>(object_.Get(), std::forward<Args>(args)...);                                 \
    } else {                                                                                                        \
        return jb::traits::method_return_t<return_type>(name ## _.template call<false>(object_.Get(), std::forward<Args>(args)...)); \
    }                                                                                                               \
}

#define JBRIDGE_DEFINE_ALIAS_METHOD(return_type, alias_name, name, ...)                                             \
template<typename ...Args>                                                                                          \
auto alias_name(Args&&... args) {                                                                                   \
    static auto name ## _ = jb::detail::CreateMethod<false, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name); \
    if constexpr (std::is_void_v<return_type>) {                                                                    \
        name ## _.template call<false>(object_.Get(), std::forward<Args>(args)...);                                 \
    } else {                                                                                                        \
        return jb::traits::method_return_t<return_type>(name ## _.template call<false>(object_.Get(), std::forward<Args>(args)...)); \
    }                                                                                                               \
}

#define JBRIDGE_DEFINE_STATIC_METHOD(return_type, name, ...)                                                        \
template<typename ...Args>                                                                                          \
static auto name(Args&&... args) {                                                                                  \
    static auto name ## _ = jb::detail::CreateMethod<true, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name); \
    if constexpr (std::is_void_v<return_type>) {                                                                    \
        name ## _.template call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...);    \
    } else {                                                                                                        \
        return jb::traits::method_return_t<return_type>(name ## _.template call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...)); \
    }                                                                                                               \
}

#define JBRIDGE_DEFINE_STATIC_ALIAS_METHOD(return_type, alias_name, name, ...)                                      \
template<typename ...Args>                                                                                          \
static auto alias_name(Args&&... args) {                                                                            \
    static auto name ## _ = jb::detail::CreateMethod<true, return_type __VA_OPT__(,) __VA_ARGS__>(CLASS_SIGNATURE, #name); \
    if constexpr (std::is_void_v<return_type>) {                                                                    \
        name ## _.template call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...);    \
    } else {                                                                                                        \
        return jb::traits::method_return_t<return_type>(name ## _.template call<true>(jb::detail::jni::FindClass(CLASS_SIGNATURE), std::forward<Args>(args)...)); \
    }                                                                                                               \
}

// ============================================================================
// Macros - Field Definition
// ============================================================================

#define JBRIDGE_DEFINE_FIELD(field_type, name)                                                                      \
[[nodiscard]] decltype(auto) name() {                                                                               \
    static auto field ## _ = jb::detail::CreateField<false, jb::traits::array_wrapper_t<field_type>>(CLASS_SIGNATURE, #name); \
    return field ## _.Update(object_.Get());                                                                        \
}

#define JBRIDGE_DEFINE_STATIC_FIELD(field_type, name)                                                               \
[[nodiscard]] static decltype(auto) name() {                                                                        \
    static auto field ## _ = jb::detail::CreateField<true, jb::traits::array_wrapper_t<field_type>>(CLASS_SIGNATURE, #name); \
    return field ## _.Update(jb::detail::jni::FindClass(CLASS_SIGNATURE));                                          \
}

#endif //JBRIDGE_JBRIDGE_HPP
