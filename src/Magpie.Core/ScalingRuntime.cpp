#include "pch.h"
#include "ScalingRuntime.h"
#include <dispatcherqueue.h>
#include "Logger.h"
#include "ScalingWindow.h"

namespace Magpie::Core {

ScalingRuntime::ScalingRuntime() : _scalingThread(std::bind(&ScalingRuntime::_ScalingThreadProc, this)) {
}

ScalingRuntime::~ScalingRuntime() {
	Stop();

	if (_scalingThread.joinable()) {
		DWORD magWndThreadId = GetThreadId(_scalingThread.native_handle());
		// 持续尝试直到 _scalingThread 创建了消息队列
		while (!PostThreadMessage(magWndThreadId, WM_QUIT, 0, 0)) {
			Sleep(1);
		}
		_scalingThread.join();
	}
}

void ScalingRuntime::Start(HWND hwndSrc, ScalingOptions&& options) {
	if (_isRunning) {
		return;
	}

	_EnsureDispatcherQueue();
	_dqc.DispatcherQueue().TryEnqueue([this, hwndSrc, options(std::move(options))]() mutable {
		_IsRunning(true);
		ScalingWindow::Get().Create(GetModuleHandle(nullptr), hwndSrc, std::move(options));
	});
}

void ScalingRuntime::ToggleOverlay() {
	if (!_isRunning) {
		return;
	}

	_EnsureDispatcherQueue();
	_dqc.DispatcherQueue().TryEnqueue([]() {
		ScalingWindow::Get().ToggleOverlay();
	});
}

void ScalingRuntime::Stop() {
	if (!_isRunning) {
		return;
	}

	_EnsureDispatcherQueue();
	_dqc.DispatcherQueue().TryEnqueue([this]() {
		ScalingWindow::Get().Destroy();
	});
}

void ScalingRuntime::_ScalingThreadProc() noexcept {
	winrt::init_apartment(winrt::apartment_type::single_threaded);

	DispatcherQueueOptions dqOptions{};
	dqOptions.dwSize = sizeof(DispatcherQueueOptions);
	dqOptions.threadType = DQTYPE_THREAD_CURRENT;

	HRESULT hr = CreateDispatcherQueueController(
		dqOptions,
		(PDISPATCHERQUEUECONTROLLER*)winrt::put_abi(_dqc)
	);
	if (FAILED(hr)) {
		Logger::Get().ComError("CreateDispatcherQueueController 失败", hr);
		return;
	}

	ScalingWindow& scalingWindow = ScalingWindow::Get();

	MSG msg;
	while (true) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				scalingWindow.Destroy();
				_IsRunning(false);
				return;
			}

			DispatchMessage(&msg);
		}

		if (scalingWindow) {
			scalingWindow.Render();
			MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		} else {
			_IsRunning(false);
			WaitMessage();
		}
	}
}

void ScalingRuntime::_EnsureDispatcherQueue() const noexcept {
	while (!_dqc) {
		Sleep(0);
	}
}

void ScalingRuntime::_IsRunning(bool value) {
	if (_isRunning == value) {
		return;
	}

	_isRunning = value;
	_isRunningChangedEvent(value);
}

}