// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_pile.h"
#include "gui_common.h"

#include "imgui.h"

#include "util/str_util.h"
#include "global.h"

void gui_pile(AppState &app) {
	ImGui::Begin("Pile");

	static std::array<char, 64> nameQuery;
	if(ImGui::InputText("Name", nameQuery.data(), nameQuery.size())){
		app.pile.state.query.query = load_string(nameQuery);
		app.pile.state.query.offset = 0;
		app.pile.request.executeQuery = true;
	}

	if(ImGui::Button("<<")) {
		app.pile.state.query.offset = 0;
		app.pile.request.executeQuery = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("<")) {
		app.pile.state.query.offset -= app.pile.state.query.limit;
		app.pile.request.executeQuery = true;
	}
	ImGui::SameLine();
	if(ImGui::Button(">")) {
		app.pile.state.query.offset += app.pile.state.query.limit;
		app.pile.request.executeQuery = true;
	}
	if(app.pile.state.query.offset < 0) {
		app.pile.state.query.offset = 0;
	}

	{
		static int current_item = 0;
		const char* items[] = { "20", "40", "80", "120" };
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		if(ImGui::Combo("Rows", &current_item, items, IM_ARRAYSIZE(items))) {
			switch(current_item) {
				case 0: app.pile.state.query.limit = 20; break;
				case 1: app.pile.state.query.limit = 40; break;
				case 2: app.pile.state.query.limit = 80; break;
				case 3: app.pile.state.query.limit = 120; break;
			};
			app.pile.request.executeQuery = true;
		}
	}

	constexpr auto tblFlags =
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_Reorderable |
		ImGuiTableFlags_Hideable |
		ImGuiTableFlags_Sortable |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_SizingFixedFit;

	if(ImGui::BeginTable("Meta", 9, tblFlags)) {
		ImGui::TableSetupColumn("play",
			ImGuiTableColumnFlags_NoSort |
			ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_NoHeaderLabel,
			30.f
		);

		// KEEP IN SYNC WITH db_query
		ImGui::TableSetupColumn("file",     ImGuiTableColumnFlags_WidthStretch, 4.f);
		ImGui::TableSetupColumn("size",     ImGuiTableColumnFlags_WidthFixed, 40.f);
		ImGui::TableSetupColumn("artist",   ImGuiTableColumnFlags_WidthStretch, 2.f);
		ImGui::TableSetupColumn("name",     ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableSetupColumn("bpm",      ImGuiTableColumnFlags_WidthFixed, 0.f);
		ImGui::TableSetupColumn("duration", ImGuiTableColumnFlags_WidthFixed, 80.f);
		ImGui::TableSetupColumn("loudness", ImGuiTableColumnFlags_WidthFixed, 0.f);
		ImGui::TableSetupColumn("rating",   ImGuiTableColumnFlags_WidthFixed, 0.f);

		ImGui::TableHeadersRow();

		auto specs = ImGui::TableGetSortSpecs();
		if(specs->SpecsDirty) {
			specs->SpecsDirty = false;
			if(specs->SpecsCount > 0) {
				auto spec = specs->Specs[0];
				app.pile.state.query.sortColNr = spec.ColumnIndex + 1;
				if(spec.SortDirection == ImGuiSortDirection_Ascending) {
					app.pile.state.query.order = AppState::Pile::State::Query::Order::asc;
				} else {
					app.pile.state.query.order = AppState::Pile::State::Query::Order::dsc;
				}
			}
			app.pile.request.executeQuery = true;
		}

		for(auto &r : app.pile.state.response.rows) {
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::PushID(r.id.c_str());
			if(ImGui::Button("play")) {
				app.player.request.playId.set(r.id);
				app.player.request.play = true;
				app.player.request.next = true;
			};
			ImGui::PopID();

			ImGui::TableNextColumn(); ImGui::Text("%s", r.file_name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtSize(r.file_size).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.artist.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptInt(r.bpm).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptMs(r.duration).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.loudness).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.rating).c_str());
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
