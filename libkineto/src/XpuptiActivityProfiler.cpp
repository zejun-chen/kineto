#include "XpuptiActivityApi.h"
#include "XPUActivityProfiler.h"

#include <chrono>

namespace KINETO_NAMESPACE {

uint32_t XpuptiActivityProfilerSession::iterationCount_ = 0;
std::vector<std::array<unsigned char, 16>>
    XpuptiActivityProfilerSession::deviceUUIDs_ = {};

// =========== Session Constructor ============= //
XpuptiActivityProfilerSession::XpuptiActivityProfilerSession(
    XpuptiActivityApi& xpti,
    const libkineto::Config& config,
    const std::set<act_t>& activity_types)
    : xpti_(xpti), config_(config.clone()), activity_types_(activity_types) {
  xpti_.setMaxBufferSize(config_->activitiesMaxGpuBufferSize());
  xpti_.enableXpuptiActivities(activity_types_);
  enumDeviceUUIDs();
}

XpuptiActivityProfilerSession::~XpuptiActivityProfilerSession() {
  xpti_.clearActivities();
}

// =========== Session Public Methods ============= //
void XpuptiActivityProfilerSession::start() {
  profilerStartTs_ =
      libkineto::timeSinceEpoch(std::chrono::high_resolution_clock::now());
}

void XpuptiActivityProfilerSession::stop() {
  xpti_.disablePtiActivities(activity_types_);
  profilerEndTs_ =
      libkineto::timeSinceEpoch(std::chrono::high_resolution_clock::now());
}

void XpuptiActivityProfilerSession::processTrace(logger_t& logger) {
  traceBuffer_.span = libkineto::TraceSpan(
      profilerStartTs_, profilerEndTs_, "__xpu_profiler__");
  traceBuffer_.span.iteration = iterationCount_++;
  auto gpuBuffer = xpti_.activityBuffers();
  if (gpuBuffer) {
    xpti_.processActivities(
        *gpuBuffer,
        std::bind(
            &XpuptiActivityProfilerSession::handlePtiActivity,
            this,
            std::placeholders::_1,
            &logger));
  }
}

void XpuptiActivityProfilerSession::processTrace(
    logger_t& logger,
    libkineto::getLinkedActivityCallback get_linked_activity,
    int64_t captureWindowStartTime,
    int64_t captureWindowEndTime) {
  captureWindowStartTime_ = captureWindowStartTime;
  captureWindowEndTime_ = captureWindowEndTime;
  cpuActivity_ = get_linked_activity;
  processTrace(logger);
}

std::unique_ptr<libkineto::DeviceInfo> XpuptiActivityProfilerSession::
    getDeviceInfo() {
  return {};
}

std::vector<libkineto::ResourceInfo> XpuptiActivityProfilerSession::
    getResourceInfos() {
  return {};
}

std::unique_ptr<libkineto::CpuTraceBuffer> XpuptiActivityProfilerSession::
    getTraceBuffer() {
  return std::make_unique<libkineto::CpuTraceBuffer>(std::move(traceBuffer_));
}

void XpuptiActivityProfilerSession::pushCorrelationId(uint64_t id) {
  xpti_.pushCorrelationID(id, XpuptiActivityApi::CorrelationFlowType::Default);
}

void XpuptiActivityProfilerSession::popCorrelationId() {
  xpti_.popCorrelationID(XpuptiActivityApi::CorrelationFlowType::Default);
}

void XpuptiActivityProfilerSession::pushUserCorrelationId(uint64_t id) {
  xpti_.pushCorrelationID(id, XpuptiActivityApi::CorrelationFlowType::User);
}

void XpuptiActivityProfilerSession::popUserCorrelationId() {
  xpti_.popCorrelationID(XpuptiActivityApi::CorrelationFlowType::User);
}

void XpuptiActivityProfilerSession::enumDeviceUUIDs() {
  if (!deviceUUIDs_.empty()) {
    return;
  }
  auto platform_list = sycl::platform::get_platforms();
  // Enumerated GPU devices from the specific platform.
  for (const auto& platform : platform_list) {
    if (platform.get_backend() != sycl::backend::ext_oneapi_level_zero) {
      continue;
    }
    auto device_list = platform.get_devices();
    for (const auto& device : device_list) {
      if (device.is_gpu()) {
        if (device.has(sycl::aspect::ext_intel_device_info_uuid)) {
          deviceUUIDs_.push_back(
              device.get_info<sycl::ext::intel::info::device::uuid>());
        } else {
          std::cerr
              << "Warnings: UUID is not supported for this XPU device. The device index of records will be 0."
              << std::endl;
          deviceUUIDs_.push_back(std::array<unsigned char, 16>{});
        }
      }
    }
  }
}

DeviceIndex_t XpuptiActivityProfilerSession::getDeviceIdxFromUUID(
    const uint8_t deviceUUID[16]) {
  std::array<unsigned char, 16> key;
  memcpy(key.data(), deviceUUID, 16);
  auto it = std::find(deviceUUIDs_.begin(), deviceUUIDs_.end(), key);
  if (it == deviceUUIDs_.end()) {
    std::cerr
        << "Warnings: Can't find the legal XPU device from the given UUID."
        << std::endl;
    return static_cast<DeviceIndex_t>(0);
  }
  return static_cast<DeviceIndex_t>(std::distance(deviceUUIDs_.begin(), it));
}

// =========== ActivityProfiler Public Methods ============= //
const std::set<act_t> kXpuTypes{
    act_t::GPU_MEMCPY,
    act_t::GPU_MEMSET,
    act_t::CONCURRENT_KERNEL,
    act_t::XPU_RUNTIME,
    act_t::EXTERNAL_CORRELATION,
    act_t::OVERHEAD,
};

const std::string& XPUActivityProfiler::name() const {
  return name_;
}

const std::set<act_t>& XPUActivityProfiler::availableActivities() const {
  throw std::runtime_error(
      "The availableActivities is legacy method and should not be called by kineto");
  return kXpuTypes;
}

std::unique_ptr<libkineto::IActivityProfilerSession> XPUActivityProfiler::
    configure(
        const std::set<act_t>& activity_types,
        const libkineto::Config& config) {
  return std::make_unique<XpuptiActivityProfilerSession>(
      XpuptiActivityApi::singleton(), config, activity_types);
}

std::unique_ptr<libkineto::IActivityProfilerSession> XPUActivityProfiler::
    configure(
        int64_t ts_ms,
        int64_t duration_ms,
        const std::set<act_t>& activity_types,
        const libkineto::Config& config) {
  AsyncProfileStartTime_ = ts_ms;
  AsyncProfileEndTime_ = ts_ms + duration_ms;
  return configure(activity_types, config);
}
} // namespace KINETO_NAMESPACE
