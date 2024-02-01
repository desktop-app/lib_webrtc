// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/win/webrtc_environment_win.h"

#include "base/platform/win/base_windows_co_task_mem.h"
#include "base/weak_ptr.h"
#include "webrtc/webrtc_environment.h"

#include <MMDeviceAPI.h>
#include <winrt/base.h>

#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <propkey.h>

namespace Webrtc::Platform {
namespace {

constexpr auto kMaxNameLength = 256;

[[nodiscard]] auto RoleForType(DeviceType type) {
	return (type == DeviceType::Playback) ? eConsole : eCommunications;
}

} // namespace

class EnvironmentWin::Client
	: public winrt::implements<Client, IMMNotificationClient>
	, public base::has_weak_ptr {
public:
	Client(
		Fn<void(DeviceType, QString)> defaultChanged,
		Fn<void(QString, std::optional<DeviceStateChange>)> deviceToggled);

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
		LPCWSTR deviceId,
		const PROPERTYKEY key) override;
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override;
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override;
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
		LPCWSTR deviceId,
		DWORD newState) override;
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
		EDataFlow flow,
		ERole role,
		LPCWSTR newDefaultDeviceId) override;

private:
	Fn<void(DeviceType, QString)> _defaultChanged;
	Fn<void(QString, std::optional<DeviceStateChange>)> _deviceToggled;

};

EnvironmentWin::Client::Client(
	Fn<void(DeviceType, QString)> defaultChanged,
	Fn<void(QString, std::optional<DeviceStateChange>)> deviceToggled)
: _defaultChanged(std::move(defaultChanged))
, _deviceToggled(std::move(deviceToggled)) {
}

HRESULT STDMETHODCALLTYPE EnvironmentWin::Client::OnPropertyValueChanged(
		LPCWSTR deviceId,
		const PROPERTYKEY key) {
	return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentWin::Client::OnDeviceAdded(
		LPCWSTR deviceId) {
	const auto id = QString::fromWCharArray(deviceId);
	crl::on_main(this, [=] {
		const auto onstack = _deviceToggled;
		onstack(id, std::nullopt);
	});
	return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentWin::Client::OnDeviceRemoved(
		LPCWSTR deviceId) {
	const auto id = QString::fromWCharArray(deviceId);
	const auto change = DeviceStateChange::Disconnected;
	crl::on_main(this, [=] {
		const auto onstack = _deviceToggled;
		onstack(id, change);
	});
	return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentWin::Client::OnDeviceStateChanged(
		LPCWSTR deviceId,
		DWORD newState) {
	const auto change = (newState == DEVICE_STATE_ACTIVE)
		? DeviceStateChange::Active
		: DeviceStateChange::Inactive;
	const auto id = QString::fromWCharArray(deviceId);
	crl::on_main(this, [=] {
		const auto onstack = _deviceToggled;
		onstack(id, change);
	});
	return S_OK;
}

HRESULT STDMETHODCALLTYPE EnvironmentWin::Client::OnDefaultDeviceChanged(
		EDataFlow flow,
		ERole role,
		LPCWSTR newDefaultDeviceId) {
	const auto type = (flow == eRender)
		? DeviceType::Playback
		: DeviceType::Capture;
	if (role == RoleForType(type)) {
		const auto id = QString::fromWCharArray(newDefaultDeviceId);
		crl::on_main(this, [=] {
			const auto onstack = _defaultChanged;
			onstack(type, id);
		});
	}
	return S_OK;
}

EnvironmentWin::EnvironmentWin(not_null<EnvironmentDelegate*> delegate)
: _delegate(delegate)
#ifdef WEBRTC_TESTING_OPENAL
, _audioFallback(delegate)
#endif // WEBRTC_TESTING_OPENAL
, _cameraFallback(delegate) {
#ifndef WEBRTC_TESTING_OPENAL
	using namespace base::WinRT;
	_enumerator = TryCreateInstance<IMMDeviceEnumerator>(
		CLSID_MMDeviceEnumerator);
	if (!_enumerator) {
		const auto hr = CoInitialize(nullptr);
		if (SUCCEEDED(hr)) {
			_comInitialized = true;
			_enumerator = TryCreateInstance<IMMDeviceEnumerator>(
				CLSID_MMDeviceEnumerator);
			if (!_enumerator) {
				LOG(("Media Error: Could not create MMDeviceEnumerator."));
				return;
			}
		}
	}
	_client = winrt::make<Client>([=](DeviceType type, QString id) {
		_delegate->defaultChanged(type, DeviceChangeReason::Manual, id);
	}, [=](QString id, std::optional<DeviceStateChange> state) {
		processDeviceStateChange(id, state);
	});
	if (!_client) {
		LOG(("Media Error: Could not create IMMNotificationClient."));
		return;
	}
	const auto hr = _enumerator->RegisterEndpointNotificationCallback(
		_client.get());
	if (FAILED(hr)) {
		LOG(("Media Error: RegisterEndpointNotificationCallback failed."));
	}
#endif // !WEBRTC_TESTING_OPENAL
}

EnvironmentWin::~EnvironmentWin() {
	if (_client) {
		Assert(_enumerator != nullptr);
		_enumerator->UnregisterEndpointNotificationCallback(_client.get());
		_client = nullptr;
	}
	_enumerator = nullptr;
	if (_comInitialized) {
		CoUninitialize();
	}
}

QString EnvironmentWin::defaultId(DeviceType type) {
	if (type == DeviceType::Camera) {
		return _cameraFallback.defaultId(type);
	} else if (!_enumerator) {
#ifdef WEBRTC_TESTING_OPENAL
		return _audioFallback.defaultId(type);
#endif // WEBRTC_TESTING_OPENAL
		return {};
	}
	const auto flow = (type == DeviceType::Playback)
		? eRender
		: eCapture;
	const auto role = RoleForType(type);

	auto device = winrt::com_ptr<IMMDevice>();
	auto hr = _enumerator->GetDefaultAudioEndpoint(flow, role, device.put());
	if (FAILED(hr) || !device) {
		return {};
	}
	auto id = base::CoTaskMemString();
	hr = device->GetId(id.put());
	if (FAILED(hr) || !id || !*id.data()) {
		return {};
	}
	return QString::fromWCharArray(id.data());
}

void EnvironmentWin::processDeviceStateChange(
		const QString &id,
		std::optional<DeviceStateChange> change) {
	const auto wide = id.toStdWString();
	auto device = winrt::com_ptr<IMMDevice>();
	auto hr = _enumerator->GetDevice(wide.c_str(), device.put());
	if (FAILED(hr)) {
		return;
	} else if (const auto endpoint = device.try_as<IMMEndpoint>()) {
		auto flow = EDataFlow();
		hr = endpoint->GetDataFlow(&flow);
		if (SUCCEEDED(hr)) {
			if (!change) {
				auto state = DWORD();
				hr = device->GetState(&state);
				if (!SUCCEEDED(hr)) {
					return;
				}
				change = (state == DEVICE_STATE_ACTIVE)
					? DeviceStateChange::Active
					: DeviceStateChange::Inactive;
			}
			const auto type = (flow == eRender)
				? DeviceType::Playback
				: DeviceType::Capture;
			_delegate->deviceStateChanged(type, id, *change);
		}
	}
}

DeviceInfo EnvironmentWin::device(DeviceType type, const QString &id) {
	if (type == DeviceType::Camera) {
		return _cameraFallback.device(type, id);
	} else if (!_enumerator) {
#ifdef WEBRTC_TESTING_OPENAL
		return _audioFallback.device(type, id);
#endif // WEBRTC_TESTING_OPENAL
		return {};
	}
	const auto wide = id.toStdWString();
	auto device = winrt::com_ptr<IMMDevice>();
	auto hr = _enumerator->GetDevice(wide.c_str(), device.put());
	if (FAILED(hr) || !device) {
		return {};
	}
	auto store = winrt::com_ptr<IPropertyStore>();
	hr = device->OpenPropertyStore(STGM_READ, store.put());
	if (FAILED(hr) || !store) {
		return {};
	}

	auto name = PROPVARIANT();
	hr = store->GetValue(PKEY_Device_FriendlyName, &name);
	if (FAILED(hr)) {
		return {};
	}
	const auto guard = gsl::finally([&] { PropVariantClear(&name); });

	auto already = std::array<WCHAR, kMaxNameLength>();
	hr = PropVariantToString(name, already.data(), MAX_PATH);
	if (FAILED(hr) || !already[0]) {
		return {};
	}
	auto state = DWORD();
	hr = device->GetState(&state);
	if (FAILED(hr)) {
		return {};
	}
	return {
		.id = id,
		.name = QString::fromWCharArray(already.data()),
		.type = type,
		.inactive = (state != DEVICE_STATE_ACTIVE),
	};
}

std::vector<DeviceInfo> EnvironmentWin::devices(DeviceType type) {
	if (type == DeviceType::Camera) {
		return _cameraFallback.devices(type);
	} else if (!_enumerator) {
#ifdef WEBRTC_TESTING_OPENAL
		return _audioFallback.devices(type);
#endif // WEBRTC_TESTING_OPENAL
		return {};
	}
	const auto flow = (type == DeviceType::Playback)
		? eRender
		: eCapture;
	const auto role = eConsole;
	auto collection = winrt::com_ptr<IMMDeviceCollection>();
	auto hr = _enumerator->EnumAudioEndpoints(
		flow,
		DEVICE_STATEMASK_ALL,
		collection.put());
	if (FAILED(hr) || !collection) {
		return {};
	}
	auto count = UINT();
	hr = collection->GetCount(&count);
	if (FAILED(hr)) {
		return {};
	}
	auto result = std::vector<DeviceInfo>();
	result.reserve(count);
	for (auto i = UINT(); i != count; ++i) {
		auto device = winrt::com_ptr<IMMDevice>();
		hr = collection->Item(i, device.put());
		if (FAILED(hr)) {
			continue;
		}
		auto endpoint = device.try_as<IMMEndpoint>();
		if (!endpoint) {
			continue;
		}
		auto flow = EDataFlow();
		hr = endpoint->GetDataFlow(&flow);
		auto id = base::CoTaskMemString();
		hr = device->GetId(id.put());
		if (FAILED(hr) || !id || !*id.data()) {
			continue;
		}
		auto store = winrt::com_ptr<IPropertyStore>();
		hr = device->OpenPropertyStore(STGM_READ, store.put());
		if (FAILED(hr) || !store) {
			continue;
		}
		auto name = PROPVARIANT();
		hr = store->GetValue(PKEY_Device_FriendlyName, &name);
		if (FAILED(hr)) {
			continue;
		}
		const auto guard = gsl::finally([&] { PropVariantClear(&name); });

		auto already = std::array<WCHAR, kMaxNameLength>();
		hr = PropVariantToString(name, already.data(), MAX_PATH);
		if (FAILED(hr) || !already[0]) {
			continue;
		}
		auto state = DWORD();
		hr = device->GetState(&state);
		if (FAILED(hr)) {
			continue;
		}
		result.push_back({
			.id = QString::fromWCharArray(id.data()),
			.name = QString::fromWCharArray(already.data()),
			.type = type,
			.inactive = (state != DEVICE_STATE_ACTIVE),
		});
	}
	return result;
}

bool EnvironmentWin::refreshFullListOnChange(DeviceType type) {
	if (type == DeviceType::Camera) {
		return _cameraFallback.refreshFullListOnChange(type);
	}
#ifdef WEBRTC_TESTING_OPENAL
	return true;
#endif // WEBRTC_TESTING_OPENAL
	return false;
}

bool EnvironmentWin::desktopCaptureAllowed() const {
	return true;
}

std::optional<QString> EnvironmentWin::uniqueDesktopCaptureSource() const {
	return {};
}

void EnvironmentWin::defaultIdRequested(DeviceType type) {
	if (type == DeviceType::Camera) {
		_cameraFallback.defaultIdRequested(type);
#ifdef WEBRTC_TESTING_OPENAL
	} else {
		_audioFallback.defaultIdRequested(type);
#endif // WEBRTC_TESTING_OPENAL
	}
}

void EnvironmentWin::devicesRequested(DeviceType type) {
	if (type == DeviceType::Camera) {
		_cameraFallback.devicesRequested(type);
#ifdef WEBRTC_TESTING_OPENAL
	} else {
		_audioFallback.devicesRequested(type);
#endif // WEBRTC_TESTING_OPENAL
	}
}

DeviceResolvedId EnvironmentWin::threadSafeResolveId(
		const DeviceResolvedId &lastResolvedId,
		const QString &savedId) {
	return (lastResolvedId.type == DeviceType::Camera)
		? _cameraFallback.threadSafeResolveId(lastResolvedId, savedId)
#ifdef WEBRTC_TESTING_OPENAL
		: _audioFallback.threadSafeResolveId(lastResolvedId, savedId);
#endif // WEBRTC_TESTING_OPENAL
		: lastResolvedId;
}

std::unique_ptr<Environment> CreateEnvironment(
		not_null<EnvironmentDelegate*> delegate) {
	return std::make_unique<EnvironmentWin>(delegate);
}

} // namespace Webrtc::Platform
