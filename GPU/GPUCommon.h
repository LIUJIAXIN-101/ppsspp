#pragma once

#include "Common/Common.h"
#include "Common/MemoryUtil.h"
#include "Core/ThreadEventQueue.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"

#if defined(__ANDROID__)
#include <atomic>
#endif

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

typedef ThreadEventQueue<GPUInterface, GPUEvent, GPUEventType, GPU_EVENT_INVALID, GPU_EVENT_SYNC_THREAD, GPU_EVENT_FINISH_EVENT_LOOP> GPUThreadEventQueue;

class FramebufferManagerCommon;
class TextureCacheCommon;
class DrawEngineCommon;
class GraphicsContext;
namespace Draw {
	class DrawContext;
}

enum DrawType {
	DRAW_UNKNOWN,
	DRAW_PRIM,
	DRAW_SPLINE,
	DRAW_BEZIER,
};

enum {
	FLAG_FLUSHBEFORE = 1,
	FLAG_FLUSHBEFOREONCHANGE = 2,
	FLAG_EXECUTE = 4,  // needs to actually be executed. unused for now.
	FLAG_EXECUTEONCHANGE = 8,
	FLAG_READS_PC = 16,
	FLAG_WRITES_PC = 32,
	FLAG_DIRTYONCHANGE = 64,  // NOTE: Either this or FLAG_EXECUTE*, not both!
};

class GPUCommon : public GPUThreadEventQueue, public GPUDebugInterface {
public:
	GPUCommon(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	virtual ~GPUCommon();

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
	void Reinitialize() override;

	void BeginHostFrame() override;
	void EndHostFrame() override;

	void InterruptStart(int listid) override;
	void InterruptEnd(int listid) override;
	void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) override;
	void EnableInterrupts(bool enable) override {
		interruptsEnabled_ = enable;
	}

	void Resized() override;
	void DumpNextFrame() override;

	void ExecuteOp(u32 op, u32 diff) override;
	void PreExecuteOp(u32 op, u32 diff) override;

	bool InterpretList(DisplayList &list) override;
	virtual bool ProcessDLQueue();
	u32  UpdateStall(int listid, u32 newstall) override;
	u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, PSPPointer<PspGeListArgs> args, bool head) override;
	u32  DequeueList(int listid) override;
	int  ListSync(int listid, int mode) override;
	u32  DrawSync(int mode) override;
	int  GetStack(int index, u32 stackPtr) override;
	void DoState(PointerWrap &p) override;
	bool FramebufferDirty() override {
		SyncThread();
		return true;
	}
	bool FramebufferReallyDirty() override {
		SyncThread();
		return true;
	}
	bool BusyDrawing() override;
	u32  Continue() override;
	u32  Break(int mode) override;
	void ReapplyGfxState() override;

	void CopyDisplayToOutput() override;
	void InitClear() override;
	bool PerformMemoryCopy(u32 dest, u32 src, int size) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformMemoryDownload(u32 dest, int size) override;
	bool PerformMemoryUpload(u32 dest, int size) override;

	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;
	void NotifyVideoUpload(u32 addr, int size, int width, int format) override;
	bool PerformStencilUpload(u32 dest, int size) override;

	void Execute_OffsetAddr(u32 op, u32 diff);
	void Execute_Vaddr(u32 op, u32 diff);
	void Execute_Iaddr(u32 op, u32 diff);
	void Execute_Origin(u32 op, u32 diff);
	void Execute_Jump(u32 op, u32 diff);
	void Execute_BJump(u32 op, u32 diff);
	void Execute_Call(u32 op, u32 diff);
	void Execute_Ret(u32 op, u32 diff);
	void Execute_End(u32 op, u32 diff);

	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BoundingBox(u32 op, u32 diff);
	void Execute_BlockTransferStart(u32 op, u32 diff);

	void Execute_TexScaleU(u32 op, u32 diff);
	void Execute_TexScaleV(u32 op, u32 diff);
	void Execute_TexOffsetU(u32 op, u32 diff);
	void Execute_TexOffsetV(u32 op, u32 diff);
	void Execute_TexLevel(u32 op, u32 diff);

	void Execute_WorldMtxNum(u32 op, u32 diff);
	void Execute_WorldMtxData(u32 op, u32 diff);
	void Execute_ViewMtxNum(u32 op, u32 diff);
	void Execute_ViewMtxData(u32 op, u32 diff);
	void Execute_ProjMtxNum(u32 op, u32 diff);
	void Execute_ProjMtxData(u32 op, u32 diff);
	void Execute_TgenMtxNum(u32 op, u32 diff);
	void Execute_TgenMtxData(u32 op, u32 diff);
	void Execute_BoneMtxNum(u32 op, u32 diff);
	void Execute_BoneMtxData(u32 op, u32 diff);

	void Execute_MorphWeight(u32 op, u32 diff);

	void Execute_Unknown(u32 op, u32 diff);

	int EstimatePerVertexCost();

	// Note: Not virtual!
	inline void Flush();

	u64 GetTickEstimate() override {
		return curTickEst_;
	}

#ifdef USE_CRT_DBG
#undef new
#endif
	void *operator new(size_t s) {
		return AllocateAlignedMemory(s, 16);
	}
	void operator delete(void *p) {
		FreeAlignedMemory(p);
	}
#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

	// From GPUDebugInterface.
	bool GetCurrentDisplayList(DisplayList &list) override;
	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) override;
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override { return std::vector<std::string>(); };
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override {
		return "N/A";
	}
	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	std::vector<DisplayList> ActiveDisplayLists() override;
	void ResetListPC(int listID, u32 pc) override;
	void ResetListStall(int listID, u32 stall) override;
	void ResetListState(int listID, DisplayListState state) override;

	GPUDebugOp DissassembleOp(u32 pc, u32 op) override;
	std::vector<GPUDebugOp> DissassembleOpRange(u32 startpc, u32 endpc) override;

	void NotifySteppingEnter() override;
	void NotifySteppingExit() override;

	u32 GetRelativeAddress(u32 data) override;
	u32 GetVertexAddress() override;
	u32 GetIndexAddress() override;
	GPUgstate GetGState() override;
	void SetCmdValue(u32 op) override;

	void UpdateUVScaleOffset() {
#ifdef _M_SSE
		__m128i values = _mm_slli_epi32(_mm_load_si128((const __m128i *)&gstate.texscaleu), 8);
		_mm_storeu_si128((__m128i *)&gstate_c.uv, values);
#elif PPSSPP_PLATFORM(ARM_NEON)
		const uint32x4_t values = vshlq_n_u32(vld1q_u32(&gstate.texscaleu), 8);
		vst1q_u32(&gstate_c.uv, values);
#else
		gstate_c.uv.uScale = getFloat24(gstate.texscaleu);
		gstate_c.uv.vScale = getFloat24(gstate.texscalev);
		gstate_c.uv.uOff = getFloat24(gstate.texoffsetu);
		gstate_c.uv.vOff = getFloat24(gstate.texoffsetv);
#endif
	}

	DisplayList* getList(int listid) override {
		return &dls[listid];
	}

	const std::list<int>& GetDisplayLists() override {
		return dlQueue;
	}
	bool DecodeTexture(u8* dest, const GPUgstate &state) override {
		return false;
	}
	std::vector<FramebufferInfo> GetFramebufferList() override;
	void ClearShaderCache() override {}
	void CleanupBeforeUI() override {}

	s64 GetListTicks(int listid) override {
		if (listid >= 0 && listid < DisplayListMaxCount) {
			return dls[listid].waitTicks;
		}
		return -1;
	}

	typedef void (GPUCommon::*CmdFunc)(u32 op, u32 diff);

protected:
	void SetDrawType(DrawType type, GEPrimitiveType prim) {
		if (type != lastDraw_) {
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET | DIRTY_VERTEXSHADER_STATE);
			lastDraw_ = type;
		}
		// Prim == RECTANGLES can cause CanUseHardwareTransform to flip, so we need to dirty.
		// Also, culling may be affected so dirty the raster state.
		if ((prim == GE_PRIM_RECTANGLES) != (lastPrim_ == GE_PRIM_RECTANGLES)) {
			gstate_c.Dirty(DIRTY_RASTER_STATE | DIRTY_VERTEXSHADER_STATE);
			lastPrim_ = prim;
		}
	}

	virtual void InitClearInternal() {}
	void BeginFrame() override;
	virtual void BeginFrameInternal();
	virtual void CopyDisplayToOutputInternal() {}
	virtual void ReinitializeInternal() {}

	// To avoid virtual calls to PreExecuteOp().
	virtual void FastRunLoop(DisplayList &list) = 0;
	void SlowRunLoop(DisplayList &list);
	void UpdatePC(u32 currentPC, u32 newPC);
	void UpdateState(GPURunState state);
	void PopDLQueue();
	void CheckDrawSync();
	int  GetNextListIndex();
	void ProcessDLQueueInternal();
	virtual void ReapplyGfxStateInternal();
	virtual void FastLoadBoneMatrix(u32 target);
	void ProcessEvent(GPUEvent ev) override;
	bool ShouldExitEventLoop() override {
		return coreState != CORE_RUNNING;
	}
	virtual void FinishDeferred() {
	}

	void DoBlockTransfer(u32 skipDrawReason);

	void AdvanceVerts(u32 vertType, int count, int bytesRead) {
		if ((vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
			int indexShift = ((vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT) - 1;
			gstate_c.indexAddr += count << indexShift;
		} else {
			gstate_c.vertexAddr += bytesRead;
		}
	}

	void PerformMemoryCopyInternal(u32 dest, u32 src, int size);
	void PerformMemorySetInternal(u32 dest, u8 v, int size);
	void PerformStencilUploadInternal(u32 dest, int size);
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);

	FramebufferManagerCommon *framebufferManager_;
	TextureCacheCommon *textureCache_;
	DrawEngineCommon *drawEngineCommon_;
	ShaderManagerCommon *shaderManager_;

	GraphicsContext *gfxCtx_;
	Draw::DrawContext *draw_;

	typedef std::list<int> DisplayListQueue;

	int nextListID;
	DisplayList dls[DisplayListMaxCount];
	DisplayList *currentList;
	DisplayListQueue dlQueue;

	bool interruptRunning;
	GPURunState gpuState;
	bool isbreak;
	u64 drawCompleteTicks;
	u64 busyTicks;

	int downcount;
	u64 startingTicks;
	u32 cycleLastPC;
	int cyclesExecuted;

	bool dumpNextFrame_;
	bool dumpThisFrame_;
	bool debugRecording_;
	bool interruptsEnabled_;
	bool resized_;
	DrawType lastDraw_;
	GEPrimitiveType lastPrim_;

private:
	u64 curTickEst_;

	// Debug stats.
	double timeSteppingStarted_;
	double timeSpentStepping_;
};

struct CommonCommandTableEntry {
	uint8_t cmd;
	uint8_t flags;
	uint64_t dirty;
	GPUCommon::CmdFunc func;
};

extern const CommonCommandTableEntry commonCommandTable[];
extern size_t commonCommandTableSize;
