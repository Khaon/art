/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>
#include <vector>

#include "art_field-inl.h"
#include "class_linker-inl.h"
#include "common_compiler_test.h"
#include "mirror/method.h"
#include "scoped_thread_state_change.h"

namespace art {

class ProxyTest : public CommonCompilerTest {
 public:
  // Generate a proxy class with the given name and interfaces. This is a simplification from what
  // libcore does to fit to our test needs. We do not check for duplicated interfaces or methods and
  // we do not declare exceptions.
  mirror::Class* GenerateProxyClass(ScopedObjectAccess& soa, jobject jclass_loader,
                                    const char* className,
                                    const std::vector<mirror::Class*>& interfaces)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    mirror::Class* javaLangObject = class_linker_->FindSystemClass(soa.Self(), "Ljava/lang/Object;");
    CHECK(javaLangObject != nullptr);

    jclass javaLangClass = soa.AddLocalReference<jclass>(mirror::Class::GetJavaLangClass());

    // Builds the interfaces array.
    jobjectArray proxyClassInterfaces = soa.Env()->NewObjectArray(interfaces.size(), javaLangClass,
                                                                  nullptr);
    soa.Self()->AssertNoPendingException();
    for (size_t i = 0; i < interfaces.size(); ++i) {
      soa.Env()->SetObjectArrayElement(proxyClassInterfaces, i,
                                       soa.AddLocalReference<jclass>(interfaces[i]));
    }

    // Builds the method array.
    jsize methods_count = 3;  // Object.equals, Object.hashCode and Object.toString.
    for (mirror::Class* interface : interfaces) {
      mirror::ObjectArray<mirror::ArtMethod>* virtual_methods = interface->GetVirtualMethods();
      methods_count += (virtual_methods == nullptr) ? 0 : virtual_methods->GetLength();
    }
    jobjectArray proxyClassMethods = soa.Env()->NewObjectArray(
        methods_count, soa.AddLocalReference<jclass>(mirror::Method::StaticClass()), nullptr);
    soa.Self()->AssertNoPendingException();

    jsize array_index = 0;
    // Fill the method array
    mirror::ArtMethod* method = javaLangObject->FindDeclaredVirtualMethod(
        "equals", "(Ljava/lang/Object;)Z");
    CHECK(method != nullptr);
    soa.Env()->SetObjectArrayElement(
        proxyClassMethods, array_index++, soa.AddLocalReference<jobject>(
            mirror::Method::CreateFromArtMethod(soa.Self(), method)));
    method = javaLangObject->FindDeclaredVirtualMethod("hashCode", "()I");
    CHECK(method != nullptr);
    soa.Env()->SetObjectArrayElement(
        proxyClassMethods, array_index++, soa.AddLocalReference<jobject>(
            mirror::Method::CreateFromArtMethod(soa.Self(), method)));
    method = javaLangObject->FindDeclaredVirtualMethod("toString", "()Ljava/lang/String;");
    CHECK(method != nullptr);
    soa.Env()->SetObjectArrayElement(
        proxyClassMethods, array_index++, soa.AddLocalReference<jobject>(
            mirror::Method::CreateFromArtMethod(soa.Self(), method)));
    // Now adds all interfaces virtual methods.
    for (mirror::Class* interface : interfaces) {
      for (int32_t i = 0, count = interface->NumVirtualMethods(); i < count; ++i) {
        soa.Env()->SetObjectArrayElement(
            proxyClassMethods, array_index++, soa.AddLocalReference<jobject>(
                mirror::Method::CreateFromArtMethod(soa.Self(), interface->GetVirtualMethod(i))));
      }
    }
    CHECK_EQ(array_index, methods_count);

    // Builds an empty exception array.
    jobjectArray proxyClassThrows = soa.Env()->NewObjectArray(0, javaLangClass, nullptr);
    soa.Self()->AssertNoPendingException();

    mirror::Class* proxyClass = class_linker_->CreateProxyClass(
        soa, soa.Env()->NewStringUTF(className), proxyClassInterfaces, jclass_loader,
        proxyClassMethods, proxyClassThrows);
    soa.Self()->AssertNoPendingException();
    return proxyClass;
  }
};

// Creates a proxy class and check ClassHelper works correctly.
TEST_F(ProxyTest, ProxyClassHelper) {
  ScopedObjectAccess soa(Thread::Current());
  jobject jclass_loader = LoadDex("Interfaces");
  StackHandleScope<4> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader*>(jclass_loader)));

  Handle<mirror::Class> I(hs.NewHandle(
      class_linker_->FindClass(soa.Self(), "LInterfaces$I;", class_loader)));
  Handle<mirror::Class> J(hs.NewHandle(
      class_linker_->FindClass(soa.Self(), "LInterfaces$J;", class_loader)));
  ASSERT_TRUE(I.Get() != nullptr);
  ASSERT_TRUE(J.Get() != nullptr);

  std::vector<mirror::Class*> interfaces;
  interfaces.push_back(I.Get());
  interfaces.push_back(J.Get());
  Handle<mirror::Class> proxy_class(hs.NewHandle(
      GenerateProxyClass(soa, jclass_loader, "$Proxy1234", interfaces)));
  interfaces.clear();  // Don't least possibly stale objects in the array as good practice.
  ASSERT_TRUE(proxy_class.Get() != nullptr);
  ASSERT_TRUE(proxy_class->IsProxyClass());
  ASSERT_TRUE(proxy_class->IsInitialized());

  EXPECT_EQ(2U, proxy_class->NumDirectInterfaces());  // Interfaces$I and Interfaces$J.
  EXPECT_EQ(I.Get(), mirror::Class::GetDirectInterface(soa.Self(), proxy_class, 0));
  EXPECT_EQ(J.Get(), mirror::Class::GetDirectInterface(soa.Self(), proxy_class, 1));
  std::string temp;
  const char* proxy_class_descriptor = proxy_class->GetDescriptor(&temp);
  EXPECT_STREQ("L$Proxy1234;", proxy_class_descriptor);
  EXPECT_EQ(nullptr, proxy_class->GetSourceFile());
}

// Creates a proxy class and check FieldHelper works correctly.
TEST_F(ProxyTest, ProxyFieldHelper) {
  ScopedObjectAccess soa(Thread::Current());
  jobject jclass_loader = LoadDex("Interfaces");
  StackHandleScope<9> hs(soa.Self());
  Handle<mirror::ClassLoader> class_loader(
      hs.NewHandle(soa.Decode<mirror::ClassLoader*>(jclass_loader)));

  Handle<mirror::Class> I(hs.NewHandle(
      class_linker_->FindClass(soa.Self(), "LInterfaces$I;", class_loader)));
  Handle<mirror::Class> J(hs.NewHandle(
      class_linker_->FindClass(soa.Self(), "LInterfaces$J;", class_loader)));
  ASSERT_TRUE(I.Get() != nullptr);
  ASSERT_TRUE(J.Get() != nullptr);

  Handle<mirror::Class> proxyClass;
  {
    std::vector<mirror::Class*> interfaces;
    interfaces.push_back(I.Get());
    interfaces.push_back(J.Get());
    proxyClass = hs.NewHandle(GenerateProxyClass(soa, jclass_loader, "$Proxy1234", interfaces));
  }

  ASSERT_TRUE(proxyClass.Get() != nullptr);
  ASSERT_TRUE(proxyClass->IsProxyClass());
  ASSERT_TRUE(proxyClass->IsInitialized());

  ArtField* instance_fields = proxyClass->GetIFields();
  EXPECT_TRUE(instance_fields == nullptr);

  ArtField* static_fields = proxyClass->GetSFields();
  ASSERT_TRUE(static_fields != nullptr);
  ASSERT_EQ(2u, proxyClass->NumStaticFields());

  Handle<mirror::Class> interfacesFieldClass(
      hs.NewHandle(class_linker_->FindSystemClass(soa.Self(), "[Ljava/lang/Class;")));
  ASSERT_TRUE(interfacesFieldClass.Get() != nullptr);
  Handle<mirror::Class> throwsFieldClass(
      hs.NewHandle(class_linker_->FindSystemClass(soa.Self(), "[[Ljava/lang/Class;")));
  ASSERT_TRUE(throwsFieldClass.Get() != nullptr);

  // Test "Class[] interfaces" field.
  ArtField* field = &static_fields[0];
  EXPECT_STREQ("interfaces", field->GetName());
  EXPECT_STREQ("[Ljava/lang/Class;", field->GetTypeDescriptor());
  EXPECT_EQ(interfacesFieldClass.Get(), field->GetType<true>());
  std::string temp;
  EXPECT_STREQ("L$Proxy1234;", field->GetDeclaringClass()->GetDescriptor(&temp));
  EXPECT_FALSE(field->IsPrimitiveType());

  // Test "Class[][] throws" field.
  field = &static_fields[1];
  EXPECT_STREQ("throws", field->GetName());
  EXPECT_STREQ("[[Ljava/lang/Class;", field->GetTypeDescriptor());
  EXPECT_EQ(throwsFieldClass.Get(), field->GetType<true>());
  EXPECT_STREQ("L$Proxy1234;", field->GetDeclaringClass()->GetDescriptor(&temp));
  EXPECT_FALSE(field->IsPrimitiveType());
}

}  // namespace art
