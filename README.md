# JBridge
JBridge is a single-header wrapper library for the Java Native Interface (JNI), written in C++20.

## Features
- **Type-safe JNI wrapper** with compile-time signature generation
- **Cyclic reference support** via forward declaration macros
- **Automatic array wrapping** for primitive and object arrays
- **RAII-based global reference management**
- **C++20 concepts and consteval** for better compile-time checks

## Requirements
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- JNI headers

## Setup
**Please do not forget** to call `jb::Init()`
```cpp
#include "<your_path>/jbridge.hpp"

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    jb::Init(vm);
    return JNI_VERSION_1_6;
}
```

## Provided Macros/Functions/Classes

### Macros

#### `JBRIDGE_DECLARE_CLASS(package, class_name)`
Forward declare a mirror class to enable cyclic references between classes.

- `@param {package}`: Namespace-style package required.
- `@param {class_name}`: Class name required.

usage:
```cpp
// Forward declarations (enables cyclic references)
JBRIDGE_DECLARE_CLASS(java::lang, Class)
JBRIDGE_DECLARE_CLASS(java::lang::reflect, Method)

// Now Class can reference Method and vice versa
JBRIDGE_DEFINE_CLASS(java::lang, Class, {
    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(Class)
    JBRIDGE_DEFINE_METHOD(java::lang::reflect::Method, getDeclaredMethod, jstring, Class[])
})

JBRIDGE_DEFINE_CLASS(java::lang::reflect, Method, {
    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(Method)
    JBRIDGE_DEFINE_METHOD(java::lang::Class, getDeclaringClass)
})
```

___
#### `JBRIDGE_DEFINE_CLASS(package, class_name, class_scope)`
Define java class

- `@param {package}`: Namespace-style package required.
- `@param {class_name}`: Class name required.
- `@param {class_scope}`: Class scope required.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {
})
```

___
#### `JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(class_name)`
Enable defined class to construct with `jobject`

- `@param {class_name}`: Class name required.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {

    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(YourClass)

})
```

___
#### `JBRIDGE_DEFINE_METHOD(return_type, name, ...)`
Define java instance method

- `@param {return_type}`: Return type required.
- `@param {name}`: Method name required.
- `@param {...}`: Parameter types optional.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {
    
    JBRIDGE_DEFINE_METHOD(int, intMethod)
    
    JBRIDGE_DEFINE_METHOD(bool, boolMethod, int)
    
    JBRIDGE_DEFINE_METHOD(void, voidMethod, jstring)

})
```

___
#### `JBRIDGE_DEFINE_ALIAS_METHOD(return_type, alias_name, name, ...)`
Define java instance method with an alias name (useful for overloaded methods)

- `@param {return_type}`: Return type required.
- `@param {alias_name}`: Alias method name required.
- `@param {name}`: Original method name required.
- `@param {...}`: Parameter types optional.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {
    
    JBRIDGE_DEFINE_ALIAS_METHOD(void, overloadMethodFloat, overloadMethod, float)
    
    JBRIDGE_DEFINE_ALIAS_METHOD(void, overloadMethodInt, overloadMethod, int)

})
```

___
#### `JBRIDGE_DEFINE_STATIC_METHOD(return_type, name, ...)`
Define java static method

- `@param {return_type}`: Return type required.
- `@param {name}`: Method name required.
- `@param {...}`: Parameter types optional.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {
    
    JBRIDGE_DEFINE_STATIC_METHOD(float, staticFloatMethod)

})
```

___
#### `JBRIDGE_DEFINE_STATIC_ALIAS_METHOD(return_type, alias_name, name, ...)`
Define java static method with an alias name

- `@param {return_type}`: Return type required.
- `@param {alias_name}`: Alias method name required.
- `@param {name}`: Original method name required.
- `@param {...}`: Parameter types optional.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {
    
    JBRIDGE_DEFINE_STATIC_ALIAS_METHOD(void, staticOverloadMethodFloat, staticOverloadMethod, float)
    
    JBRIDGE_DEFINE_STATIC_ALIAS_METHOD(void, staticOverloadMethodInt, staticOverloadMethod, int)

})
```

___
#### `JBRIDGE_DEFINE_FIELD(field_type, name)`
Define java instance field

- `@param {field_type}`: Field type required.
- `@param {name}`: Field name required.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {

    JBRIDGE_DEFINE_FIELD(int, intField)

})
```

___
#### `JBRIDGE_DEFINE_STATIC_FIELD(field_type, name)`
Define java static field

- `@param {field_type}`: Field type required.
- `@param {name}`: Field name required.

usage:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {

    JBRIDGE_DEFINE_STATIC_FIELD(double, staticDoubleField)

})
```

___
### Functions

#### `<Defined-Class>::new_(...)`
Call constructor of your defined class.

- `@params {...}`: Parameter matched arguments required.

```cpp
// Assume that YourClass(int, float) is defined
void someFunction() {
    
    auto your_class = package::YourClass::new_(1, 3.14f);
    
}
```

___
#### `jb::MakeGlobalRef(Object object)`
Make object global ref and return as shared_ptr.

- `@params {object}`: Instance of the defined class or a jobject group object required.
- `@returns {std::shared_ptr<Object>}`: RAII resource management.

usage:
```cpp
// Assume that YourClass is defined
void someFunction() {
    
    auto your_class = package::YourClass::new_(1, 3.14f);
    
    auto as_global = jb::MakeGlobalRef(your_class);
    
}
```

___
### Classes

#### `jb::ObjectRef<Mirror>`
A lazy reference wrapper for potentially incomplete mirror types. Enables cyclic references between classes.

```cpp
// When Method is forward-declared but not yet complete
jb::ObjectRef<Method> ref = cls.getDeclaredMethod("foo");

// Implicit conversion to jobject (always available)
jobject raw = ref;

// Implicit conversion to Mirror (when Mirror is complete)
Method m = ref;

// Explicit get
jobject obj = ref.get();
```

___
#### `jb::(BooleanArray, ByteArray, CharArray, ShortArray, IntArray, LongArray, FloatArray, DoubleArray)`
Wrapper classes for j\<primitive>Array provide the following:
```cpp
::Ctor()
::Ctor(size_t size)
::Ctor(j<primitive>Array array)
::Ctor(jobject array)
::Copy/MoveCtor
::operator=(...)                            // Copy and Move
::operator[](size_t index)
::Get(size_t index)
::Set(size_t index, j<primitive> element)
::Size()                                    // returns as size_t
::Raw()                                     // returns raw array pointer
::begin(), end()                            // supports range-based for
::data()                                    // returns element pointer
::empty()                                   // returns true if size == 0
```

___
#### `jb::ObjectArray<Defined-Class>`
A wrapper class for jobjectArray provides:
```cpp
::Ctor()
::Ctor(size_t size)
::Ctor(jobjectArray array)
::Ctor(jobject array)
::Copy/MoveCtor
::operator=(...)                            // Copy and Move
::operator[](size_t index)                  // returns ObjectRef<Defined-Class>
::Get(size_t index)                         // returns ObjectRef<Defined-Class>
::GetAsRaw(size_t index)                    // returns as jobject
::Set(size_t index, jobject element)
::Size()                                    // returns as size_t
::Raw()                                     // returns raw array pointer
::empty()                                   // returns true if size == 0
```
Note: jobjectArray is not contiguous in memory, so range-based for loops are not supported.

___
#### `jb::JniObject<JObject-Type>`
A class that encodes and marks the holding object as a JNI object, enabling a global reference.

usage:
```cpp
struct ObjectHolder {
    jb::JniObject<jobject> o1_;
    jb::JniObject<jclass> c1_;
    
    ObjectHolder(jobject o1, jclass c1) : o1_(o1), c1_(c1) {}
};

void someFunction(jobject o1, jclass c1) {
    ObjectHolder holder{o1, c1};
    auto as_global = jb::MakeGlobalRef(holder);
}
```

___
## Specification

### Auto Array Wrapping
For return value signature generation, the return_type of the DEFINE macro is required to specify the type as it is, but there is a limit to adjusting the response to the return value type. Therefore, JBridge converts all array types to wrapper classes.

For example:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {

    JBRIDGE_DEFINE_METHOD(double[], returnDoubleArray)

})
```
is reinterpreted as:
```cpp
JBRIDGE_DEFINE_CLASS(your::package, YourClass, {

    JBRIDGE_DEFINE_METHOD(jb::DoubleArray, returnDoubleArray)

})
```

### Cyclic Reference Resolution
JBridge supports cyclic references between mirror classes using `JBRIDGE_DECLARE_CLASS`. This macro forward-declares the class and registers its JNI signature via a trait specialization, allowing other classes to reference it before its full definition.

```cpp
// Step 1: Forward declare all classes involved in cyclic references
JBRIDGE_DECLARE_CLASS(java::lang, Class)
JBRIDGE_DECLARE_CLASS(java::lang::reflect, Method)

// Step 2: Define classes (order doesn't matter now)
JBRIDGE_DEFINE_CLASS(java::lang, Class, {
    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(Class)
    JBRIDGE_DEFINE_METHOD(java::lang::reflect::Method[], getDeclaredMethods)
})

JBRIDGE_DEFINE_CLASS(java::lang::reflect, Method, {
    JBRIDGE_REQUIRE_EXTENDED_CONSTRUCTION(Method)
    JBRIDGE_DEFINE_METHOD(java::lang::Class, getDeclaringClass)
    JBRIDGE_DEFINE_METHOD(java::lang::Class, getReturnType)
})
```

### C++20 Features Used
- **Concepts**: Type constraints for JNI types, mirror classes, etc.
- **consteval**: Compile-time signature generation
- **std::span**: Safe array views
- **[[nodiscard]]**: Prevent ignoring important return values
- **Designated initializers**: Cleaner struct initialization

___
## Migration from C++17 Version

If upgrading from the C++17 version:

1. **Compiler**: Ensure your compiler supports C++20
2. **Cyclic references**: Use `JBRIDGE_DECLARE_CLASS` for forward declarations instead of manual workarounds
3. **ObjectRef**: Methods returning incomplete types now return `ObjectRef<T>` which implicitly converts to `T` when complete

___
## ToDo

- [ ] Support hidden API access
- [ ] Support class extension
- [ ] Support defining array fields
- [ ] JNI exception handling improvements
