#include "isolate_setup.h"

#include <iostream>

#include <include/dart_api.h>
#include <include/dart_embedder_api.h>

#include <bin/dfe.h>
#include <bin/isolate_data.h>

using namespace dart::bin;

extern "C" {
extern const uint8_t kDartCoreIsolateSnapshotData[];
extern const uint8_t kDartCoreIsolateSnapshotInstructions[];
}

Dart_Isolate CreateKernelIsolate(const char* script_uri,
                                 const char* main,
                                 const char* package_root,
                                 const char* package_config,
                                 Dart_IsolateFlags* flags,
                                 void* callback_data,
                                 char** error) {
  const char* kernel_snapshot_uri = dfe.frontend_filename();
  const char* uri =
      kernel_snapshot_uri != NULL ? kernel_snapshot_uri : script_uri;

  const uint8_t* kernel_service_buffer = nullptr;
  intptr_t kernel_service_buffer_size = 0;
  dfe.LoadKernelService(&kernel_service_buffer, &kernel_service_buffer_size);

  IsolateGroupData* isolate_group_data =
      new IsolateGroupData(uri, package_config, nullptr, false);
  isolate_group_data->SetKernelBufferUnowned(
      const_cast<uint8_t*>(kernel_service_buffer), kernel_service_buffer_size);
  IsolateData* isolate_data = new IsolateData(isolate_group_data);

  dart::embedder::IsolateCreationData data = {script_uri, main, flags,
                                              isolate_group_data, isolate_data};

  Dart_Isolate isolate = dart::embedder::CreateKernelServiceIsolate(
      data, kernel_service_buffer, kernel_service_buffer_size, error);
  if (isolate == nullptr) {
    std::cerr << "Error creating kernel isolate: " << *error << std::endl;
    delete isolate_data;
    delete isolate_group_data;
    return isolate;
  }

  // Needed?
  // Dart_EnterIsolate(isolate);
  // Dart_SetLibraryTagHandeler(LibraryTagHandler);
  // SetupCoreLibraries(isolate, isolate_data, false, nullptr);

  return isolate;
}

Dart_Isolate CreateVmServiceIsolate(const char* script_uri,
                                    const char* main,
                                    const char* package_root,
                                    const char* package_config,
                                    Dart_IsolateFlags* flags,
                                    void* callback_data,
                                    char** error) {
  IsolateGroupData* isolate_group_data =
      new IsolateGroupData(script_uri, package_config, nullptr, false);
  IsolateData* isolate_data = new IsolateData(isolate_group_data);

  flags->load_vmservice_library = true;
  const uint8_t* isolate_snapshot_data = kDartCoreIsolateSnapshotData;
  const uint8_t* isolate_snapshot_instructions =
      kDartCoreIsolateSnapshotInstructions;

  dart::embedder::IsolateCreationData data = {script_uri, main, flags,
                                              isolate_group_data, isolate_data};

  dart::embedder::VmServiceConfiguration vm_config = {
      "127.0.0.1", 5858, nullptr, false, true, false};

  Dart_Isolate isolate = dart::embedder::CreateVmServiceIsolate(
      data, vm_config, isolate_snapshot_data, isolate_snapshot_instructions,
      error);
  if (isolate == nullptr) {
    std::cerr << "Error creating VM Service Isolate: " << *error << std::endl;
    delete isolate_data;
    delete isolate_group_data;
    return nullptr;
  }

  // TODO -- Dart_SetEnvironmentCallback(DartUtils::EnvironmentCallback)

  return isolate;
}

Dart_Isolate CreateIsolate(bool is_main_isolate,
                           const char* script_uri,
                           const char* name,
                           const char* packages_config,
                           Dart_IsolateFlags* flags,
                           void* callback_data,
                           char** error) {
  uint8_t* kernel_buffer = nullptr;
  intptr_t kernel_buffer_size;

  PathSanitizer script_uri_sanitizer(script_uri);
  PathSanitizer packages_config_sanitiser(packages_config);

  dfe.ReadScript(script_uri, &kernel_buffer, &kernel_buffer_size);
  flags->null_safety = true;

  IsolateGroupData* isolate_group_data =
      new IsolateGroupData(script_uri, packages_config, nullptr, false);
  isolate_group_data->SetKernelBufferNewlyOwned(kernel_buffer,
                                                kernel_buffer_size);

  const uint8_t* platform_kernel_buffer = nullptr;
  intptr_t platform_kernel_buffer_size = 0;
  dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
  if (platform_kernel_buffer == nullptr) {
    platform_kernel_buffer = kernel_buffer;
    platform_kernel_buffer_size = kernel_buffer_size;
  }

  IsolateData* isolate_data = new IsolateData(isolate_group_data);
  Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
      script_uri, name, platform_kernel_buffer, platform_kernel_buffer_size,
      flags, isolate_group_data, isolate_data, error);

  if (isolate == nullptr) {
    std::cerr << "Error creating isolate " << name << ": " << *error
              << std::endl;

    delete isolate_data;
    delete isolate_group_data;
    return nullptr;
  }

  std::cout << "Created isolate " << name << std::endl;
  Dart_EnterScope();

  // TODO - Set Library Tag Handler, SetupCoreLibraries
  const char* resolved_packages_config = nullptr;
  //SetupCoreLibraries(isolate, isolate_data, true, &resolvedPackagesConfig);

  if (kernel_buffer == nullptr && !Dart_IsKernelIsolate(isolate)) {
    uint8_t* application_kernel_buffer = nullptr;
    intptr_t application_kernel_buffer_size = 0;
    int exit_code = 0;
    dfe.CompileAndReadScript(script_uri, &application_kernel_buffer,
                             &application_kernel_buffer_size, error, &exit_code,
                             resolved_packages_config, true);
    if (application_kernel_buffer == nullptr) {
      std::cerr << "Error reading Dart script " << script_uri << ": " << *error
                << std::endl;
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }

    isolate_group_data->SetKernelBufferNewlyOwned(
        application_kernel_buffer, application_kernel_buffer_size);
    kernel_buffer = application_kernel_buffer;
    kernel_buffer_size = application_kernel_buffer_size;
  }

  if (kernel_buffer != nullptr) {
    Dart_Handle result =
        Dart_LoadScriptFromKernel(kernel_buffer, kernel_buffer_size);
    if (Dart_IsError(result)) {
      std::cerr << "Error loading script: " << Dart_GetError(result)
                << std::endl;
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
  }

  Dart_ExitScope();
  Dart_ExitIsolate();
  *error = Dart_IsolateMakeRunnable(isolate);

  if (*error != nullptr) {
    std::cerr << "Error making isolate runnable: " << error << std::endl;
    Dart_EnterIsolate(isolate);
    Dart_ShutdownIsolate();
    return nullptr;
  }

  return isolate;
}