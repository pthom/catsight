#include <Common.h>
#include <Tabs/CodeTab.h>
#include <Inspector.h>
#include <Resources.h>
#include <Helpers/CodeButton.h>
#include <Helpers/DataButton.h>

#include <hello_imgui.h>

CodeTab::CodeTab(Inspector* inspector, const s2::string& name, uintptr_t p)
	: MemoryTab(inspector, name, p)
{
	//TODO: Different parameters for 32 bit
	ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);
	ZydisFormatterInit(&m_formatter, ZYDIS_FORMATTER_STYLE_INTEL);

	m_showScrollBar = false;
}

CodeTab::~CodeTab()
{
}

s2::string CodeTab::GetLabel()
{
	return s2::strprintf(ICON_FA_CODE " %s (" POINTER_FORMAT ")###Code", MemoryTab::GetLabel().c_str(), m_region.m_start + m_baseOffset);
}

void CodeTab::Render()
{
	m_lineDetails.ensure_memory(m_itemsPerPage + 1);
	while (m_lineDetails.len() < m_itemsPerPage + 1) {
		m_lineDetails.add();
	}

	auto handle = m_inspector->m_processHandle;

	size_t base = m_region.m_start;
	size_t size = m_region.m_end - m_region.m_start;

	int bytesOffset = 0;
	for (int i = 0; i < m_itemsPerPage + 1; i++) {
		uintptr_t offset = m_topOffset + bytesOffset;
		uintptr_t address = base + offset;

		intptr_t displayOffset = (intptr_t)offset - (intptr_t)m_baseOffset;

		assert((size_t)i < m_lineDetails.len());
		auto& line = m_lineDetails[i];

		ImGui::PushID((void*)address);
		ImGui::PushFont(Resources::FontMono);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.5f, .5f, .5f, 1));
		ImGui::Text(POINTER_FORMAT, address);
		ImGui::PopStyleColor();
		ImGui::SameLine();

		float column = 130.0f;

		if (ImGui::Button("$")) {
			m_baseOffset = offset;
		}

		column += 30;
		ImGui::SameLine(column);

		ImGui::PushStyleColor(ImGuiCol_Text, ImColor::HSV(0.0f, 0.65f, 1.0f).Value);
		if (displayOffset == 0) {
			ImGui::TextUnformatted("$ ==>");
		} else {
			const char* format = "$+" OFFSET_FORMAT;
			if (displayOffset < 0) {
				format = "$-" OFFSET_FORMAT;
			}
			uintptr_t absDisplayOffset = (uintptr_t)(displayOffset < 0 ? displayOffset * -1 : displayOffset);
			ImGui::Text(format, absDisplayOffset);
		}
		ImGui::PopStyleColor();

		column += 70;
		ImGui::SameLine(column);

		uint8_t buffer[MAX_INSTRUCTION_SIZE];
		size_t bufferSize = handle->ReadMemory(address, buffer, sizeof(buffer));

		// Decode instruction
		ZydisDecodedInstruction instr;
		bool valid = ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&m_decoder, buffer, bufferSize, &instr));
		size_t instrSize = valid ? instr.length : 1;
		bytesOffset += instrSize;

		// Set instruction color
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		if (instr.mnemonic == ZYDIS_MNEMONIC_NOP || instr.mnemonic == ZYDIS_MNEMONIC_INT3) {
			color = ImVec4(.5f, .5f, .5f, 1);
		} else if (instr.meta.category == ZYDIS_CATEGORY_CALL) {
			color = ImVec4(1, .5f, .5f, 1);
		} else if (instr.meta.category == ZYDIS_CATEGORY_RET) {
			color = ImVec4(1, 1, .5f, 1);
		} else if (instr.meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) {
			color = ImVec4(.5f, 1, 1, 1);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, color);

		// Show instruction bytes
		size_t sizePrefix = instr.raw.prefix_count;
		size_t sizeGroup1 = instr.raw.disp.size / 8;
		size_t sizeGroup2 = instr.raw.imm[0].size / 8;
		size_t sizeGroup3 = instr.raw.imm[1].size / 8;
		size_t sizeOpcode = instr.length - sizePrefix - sizeGroup1 - sizeGroup2 - sizeGroup3;

		s2::string strBytes;
		for (size_t j = 0; j < instrSize; j++) {
			if (j > 0) {
				if (j == sizePrefix) {
					strBytes.append(':');
				} else if (j == sizePrefix + sizeOpcode) {
					strBytes.append(' ');
				} else if (j == sizePrefix + sizeOpcode + sizeGroup1) {
					strBytes.append(' ');
				} else if (j == sizePrefix + sizeOpcode + sizeGroup1 + sizeGroup2) {
					strBytes.append(' ');
				} else if (j == sizePrefix + sizeOpcode + sizeGroup1 + sizeGroup2 + sizeGroup3) {
					strBytes.append(' ');
				}
			}

			strBytes.appendf("%02X", buffer[j]);
		}
		ImGui::Text("%s", strBytes.c_str());

		column += 180;
		ImGui::SameLine(column);

		// Show formatted instruction text
		char instructionText[256] = "??";
		if (valid) {
			ZydisFormatterFormatInstruction(&m_formatter, &instr, instructionText, sizeof(instructionText), address);
		}

		ImGui::Text("%s", instructionText);
		ImGui::PopStyleColor();

		ImGui::PopFont();
		ImGui::SameLine();

		if (valid) {
			for (uint8_t j = 0; j < instr.operand_count; j++) {
				uintptr_t operandValue = 0;

				auto& op = instr.operands[j];
				if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
					if (op.imm.is_relative) {
						ZydisCalcAbsoluteAddress(&instr, &op, address, &operandValue);
					} else {
						operandValue = op.imm.value.u;
					}
				} else if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
					ZydisCalcAbsoluteAddress(&instr, &op, address, &operandValue);
				}

				if (handle->IsReadableMemory(operandValue)) {
					//ImGui::TextDisabled(POINTER_FORMAT, operandValue);
					//ImGui::SameLine();

					const char* str = DetectString(operandValue);
					if (str != nullptr) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, .5f, 1));
						ImGui::Text("\"%s\"", str);
						ImGui::PopStyleColor();
						ImGui::SameLine();
					}

					if (m_invalidated) {
						line.m_memoryExecutable = false;

						ProcessMemoryRegion region;
						if (handle->GetMemoryRegion(operandValue, region)) {
							line.m_memoryExecutable = region.IsExecute();
						}
					}

					if (line.m_memoryExecutable) {
						Helpers::CodeButton(m_inspector, operandValue);
						ImGui::SameLine();
					} else {
						Helpers::DataButton(m_inspector, operandValue);
						ImGui::SameLine();
					}
				}
			}
		}

		ImGui::NewLine();
		ImGui::PopID();
	}
}

intptr_t CodeTab::GetScrollAmount(int wheel)
{
	const int numInstructionsPerRotation = 3;
	int numInstructions = numInstructionsPerRotation * abs(wheel);

	uintptr_t address = m_region.m_start + m_topOffset;

	if (wheel < 0) {
		// Scroll down
		int bytesOffset = 0;
		for (int i = 0; i < numInstructions; i++) {
			uint8_t buffer[MAX_INSTRUCTION_SIZE];
			size_t bufferSize = m_inspector->m_processHandle->ReadMemory(address + bytesOffset, buffer, sizeof(buffer));

			ZydisDecodedInstruction instr;
			if (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&m_decoder, buffer, bufferSize, &instr))) {
				bytesOffset += instr.length;
			} else {
				bytesOffset++;
			}
		}
		return bytesOffset;

	} else {
		// Scroll up
		size_t rvaSize = (numInstructions + 3) * MAX_INSTRUCTION_SIZE;
		size_t bufferSize = rvaSize + 1 + MAX_INSTRUCTION_SIZE;
		uint8_t* buffer = (uint8_t*)alloca(rvaSize);

		uintptr_t start = address - rvaSize;
		m_inspector->m_processHandle->ReadMemory(start, buffer, bufferSize);

		uintptr_t bufferOffset = DisassembleBack(buffer, bufferSize, rvaSize, numInstructions);
		uintptr_t newOffset = start + bufferOffset;

		return newOffset - address;
	}
}

uintptr_t CodeTab::DisassembleBack(const uint8_t* data, size_t size, uintptr_t ip, int n)
{
	// Function borrowed from x64dbg (QBeaEngine::DisassembleBack) which borrowed it from ollydbg

	if (data == nullptr) {
		return 0;
	}

	if (n < 0) {
		n = 0;
	} else if (n > 127) {
		n = 127;
	}

	if (ip >= size) {
		ip = size - 1;
	}

	if (n == 0 || ip < (uintptr_t)n) {
		return ip;
	}

	uintptr_t back = (n + 3) * MAX_INSTRUCTION_SIZE;
	if (ip < back) {
		back = ip;
	}

	uintptr_t addr = ip - back;
	const uint8_t* pdata = data + addr;

	uintptr_t abuf[128];
	int i = 0;
	while (addr < ip) {
		abuf[i % 128] = addr;

		uintptr_t length = 2;
		ZydisDecodedInstruction instr;
		if (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&m_decoder, pdata, size, &instr))) {
			length = instr.length;
		}

		pdata += length;
		addr += length;
		back -= length;
		size -= length;

		i++;
	}

	if (i < n) {
		return abuf[0];
	}
	return abuf[(i - n + 128) % 128];
}