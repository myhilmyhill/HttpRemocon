#pragma once

#include "LibISDB/LibISDB/LibISDB.hpp"
#include "LibISDB/LibISDB/Engine/TSEngine.hpp"
#include "LibISDB/LibISDB/Base/Stream.hpp"
#include "LibISDB/LibISDB/Base/Debug.hpp"
#include "LibISDB/LibISDB/Filters/StreamSourceFilter.hpp"

using namespace LibISDB;

class Engine : public TSEngine {
public:
	bool OpenSource(Stream* stream) {

		CloseSource();

		auto StreamSource = dynamic_cast<StreamSourceFilter*>(m_pSource);
		if (LIBISDB_TRACE_ERROR_IF(StreamSource == nullptr)) {
			return false;
		}

		const FilterGraph::IDType SourceFilterID = m_FilterGraph.GetFilterID(m_pSource);

		m_FilterGraph.DisconnectFilter(SourceFilterID, FilterGraph::ConnectDirection::Downstream);

		// ソースフィルタを開く
		Log(Logger::LogType::Information, LIBISDB_STR("Opening source..."));
		bool OK = StreamSource->OpenSource(stream);
		if (!OK) {
			SetError(m_pSource->GetLastErrorDescription());
		}

		m_FilterGraph.ConnectFilter(SourceFilterID, FilterGraph::ConnectDirection::Downstream);

		if (!OK)
			return false;

		if (m_StartStreamingOnSourceOpen) {
			Log(Logger::LogType::Information, LIBISDB_STR("Starting streaming..."));
			if (!m_pSource->StartStreaming()) {
				SetError(m_pSource->GetLastErrorDescription());
				return false;
			}
		}

		//ResetEngine();
		ResetStatus();

		return true;
	}
};
