
#include "VMX.h"
#include "msr.h"

#define VMWRITE_ERR_QUITB(field, val)  status = __vmx_vmwrite((field), (val));  if (status) return FALSE;

GUEST_STATE	g_guestState;

extern void  hv_exit();

extern
void
__fastcall
get_guest_exit(
	__out ULONG_PTR* guestRip,
	__out ULONG_PTR* guestRsp
	);

//////////////////////////////////////////////////////////////////////

BOOLEAN
VmcsInit()
{
    UCHAR status;
    ULONG_PTR m_exceptionMask = 0;
    PHYSICAL_ADDRESS addr;
    //
    ULONG_PTR guest_rsp;
    ULONG_PTR guest_rip;

    get_guest_exit(&guest_rsp, &guest_rip);

	if (!(__readmsr(IA32_FEATURE_CONTROL_CODE) & FEATURE_CONTROL_VMXON_ENABLED))
    {
        DbgPrint("IA32_FEATURE_CONTROL bit[2] = 0!\n");
        return FALSE;
    }

    if (!(__readmsr(IA32_FEATURE_CONTROL_CODE) & FEATURE_CONTROL_LOCKED))
    {
        DbgPrint("IA32_FEATURE_CONTROL bit[0] = 0!\n");
        return FALSE;
    }

    DbgPrint("=Virtualization is enabled! exc mask : 0x%x, RSP:%p RIP:%p \n", m_exceptionMask, guest_rsp, guest_rip);

    __cli();
    __writecr0(g_guestState.CR0);
    __writecr4(g_guestState.CR4);
    __sti();

	*(ULONG_PTR)(g_guestState.VMCS)  = __readmsr(IA32_VMX_BASIC_MSR_CODE);
	*(ULONG_PTR)(g_guestState.VMXON) = __readmsr(IA32_VMX_BASIC_MSR_CODE);

    addr = MmGetPhysicalAddress(g_guestState.VMXON);
	__vmx_on(addr);

    addr = MmGetPhysicalAddress(g_guestState.VMCS);
	__vmx_vmclear(addr);
	__vmx_vmptrld(addr);

	//GLOBALS
	VMWRITE_ERR_QUITB(VMX_VMCS_CTRL_ENTRY_MSR_LOAD_COUNT, 0);
	VMWRITE_ERR_QUITB(VMX_VMCS_CTRL_EXIT_MSR_LOAD_COUNT,  0);
	VMWRITE_ERR_QUITB(VMX_VMCS_CTRL_EXIT_MSR_STORE_COUNT, 0);

	if (SetCRx())          return FALSE;
	if (SetControls())     return FALSE;
	if (SetDT())           return FALSE;
	if (SetSysCall())      return FALSE;
    if (SetSegSelectors()) return FALSE;

	//GUEST
	VMWRITE_ERR_QUITB(VMX_VMCS_GUEST_LINK_PTR_FULL, -1);
	VMWRITE_ERR_QUITB(VMX_VMCS_GUEST_LINK_PTR_HIGH, -1);

	VMWRITE_ERR_QUITB(VMX_VMCS_GUEST_DEBUGCTL_FULL, __readmsr(IA32_DEBUGCTL));
	VMWRITE_ERR_QUITB(VMX_VMCS_GUEST_DEBUGCTL_HIGH, __readmsr(IA32_DEBUGCTL) >> 32);

// 	VMWRITE_ERR_QUITB(VMX_VMCS64_GUEST_FS_BASE, __readmsr(IA32_FS_BASE));
// 	VMWRITE_ERR_QUITB(VMX_VMCS64_GUEST_GS_BASE, __readmsr(IA32_GS_BASE));

    VMWRITE_ERR_QUITB(VMX_VMCS64_GUEST_RSP, guest_rsp);
    VMWRITE_ERR_QUITB(VMX_VMCS64_GUEST_RIP, guest_rip);
    VMWRITE_ERR_QUITB(VMX_VMCS_GUEST_RFLAGS, g_guestState.RFLAGS);

	VMWRITE_ERR_QUITB(VMX_VMCS_HOST_RSP, g_guestState.hvStack + PAGE_SIZE - 1);
	VMWRITE_ERR_QUITB(VMX_VMCS_HOST_RIP, hv_exit);
	
	if (m_exceptionMask)
	{
		ULONG_PTR val_state;
        ULONG_PTR exception_Bitmap;

        status = __vmx_vmread(VMX_VMCS32_GUEST_INTERRUPTIBILITY_STATE, &val_state);  if (status)  return FALSE;

        if (val_state & 3)
        {
            val_state &= ~3;
            VMWRITE_ERR_QUITB(VMX_VMCS32_GUEST_INTERRUPTIBILITY_STATE, val_state);
        }

        status = __vmx_vmread(VMX_VMCS_CTRL_EXCEPTION_BITMAP, &exception_Bitmap);  if (status)  return FALSE;

        exception_Bitmap |= m_exceptionMask;
        VMWRITE_ERR_QUITB(VMX_VMCS_CTRL_EXCEPTION_BITMAP, exception_Bitmap);
	}

	//handle pagefault via VMX_EXIT_EPT_VIOLATION
	/*
	VMWRITE_ERR_QUIT(VMX_VMCS_CTRL_EPTP_FULL, m_guestState.CR3 | VMX_EPT_MEMTYPE_WB | (VMX_EPT_PAGE_WALK_LENGTH_DEFAULT << VMX_EPT_PAGE_WALK_LENGTH_SHIFT));
	VMWRITE_ERR_QUIT(VMX_VMCS_CTRL_EPTP_HIGH, m_guestState.CR3 >> 32);
	*/

	DbgPrint("\ncr0 %p", g_guestState.CR0);	
	DbgPrint("\ncr3 %p", g_guestState.CR3);
	DbgPrint("\ncr4 %p", g_guestState.CR4);

	//descriptor tables
	DbgPrint("\nidtr base %p",  g_guestState.Idtr.base);
	DbgPrint("\nidtr limit %p", g_guestState.Idtr.limit);
	DbgPrint("\ngdtr base %p",  g_guestState.Gdtr.base);
	DbgPrint("\ngdtr limit %p", g_guestState.Gdtr.limit);

	//SELECTORS
	DbgPrint("\ncs  %p", g_guestState.Cs);
	DbgPrint("\nds  %p", g_guestState.Ds);
	DbgPrint("\nes  %p", g_guestState.Es);
	DbgPrint("\nss  %p", g_guestState.Ss);	
	DbgPrint("\nfs  %p", g_guestState.Fs);
	DbgPrint("\ngs  %p", g_guestState.Gs);	
	DbgPrint("\nldtr %p", g_guestState.Ldtr);
	DbgPrint("\ntr  %p", g_guestState.Tr);

	__vmx_vmlaunch();

	DbgPrint("\nHYPERVISOR IS NOT TURNED ON, something failed!\n");
	//DbgBreakPoint();
	return FALSE;
}

void
GetGuestState()
{
	g_guestState.CR0 = __readcr0() & __readmsr(IA32_VMX_CR0_FIXED1) | __readmsr(IA32_VMX_CR0_FIXED0) | CR0_PE | CR0_NE | CR0_PG;
    g_guestState.CR3 = __readcr3();
	g_guestState.CR4 = __readcr4() & __readmsr(IA32_VMX_CR4_FIXED1) | __readmsr(IA32_VMX_CR4_FIXED0) | CR4_VMXE | CR4_DE;

	g_guestState.RFLAGS = __readeflags();

	g_guestState.Cs = __readcs();
	g_guestState.Ds = __readds();
	g_guestState.Es = __reades();
	g_guestState.Ss = __readss();
	g_guestState.Fs = __readfs();
	g_guestState.Gs = __readgs();
    g_guestState.Ldtr = __sldt();
    g_guestState.Tr = __str();

    __sgdt(&(g_guestState.Gdtr));
    __sidt(&(g_guestState.Idtr));

	g_guestState.PIN   = __readmsr(IA32_VMX_PINBASED_CTLS);
	g_guestState.PROC  = __readmsr(IA32_VMX_PROCBASED_CTLS) | CPU_BASED_RDTSC_EXITING;
	g_guestState.EXIT  = __readmsr(IA32_VMX_EXIT_CTLS) | VMX_VMCS32_EXIT_IA32E_MODE | VMX_VMCS32_EXIT_ACK_ITR_ON_EXIT;
	g_guestState.ENTRY = __readmsr(IA32_VMX_ENTRY_CTLS) | VMX_VMCS32_ENTRY_IA32E_MODE;
	g_guestState.SEIP  = __readmsr(IA64_SYSENTER_EIP);
	g_guestState.SESP  = __readmsr(IA32_SYSENTER_ESP);

    g_guestState.VMXON = MmAllocateNonCachedMemory(PAGE_SIZE);
    RtlZeroMemory(g_guestState.VMXON, PAGE_SIZE);

	g_guestState.VMCS  = MmAllocateNonCachedMemory(PAGE_SIZE);
	RtlZeroMemory(g_guestState.VMCS,  PAGE_SIZE);

    g_guestState.hvStack =        // ������Ƿ�ҳ���ڴ棬�ұ�֤�������ڴ�����������,MmFreeContiguousMemory
        MmAllocateContiguousMemory(HYPERVISOR_STACK_PAGE, (PHYSICAL_ADDRESS)0xFFFFFFFF00000000);
    RtlZeroMemory(g_guestState.hvStack, HYPERVISOR_STACK_PAGE);
}

UCHAR
SetCRx()
{
	UCHAR status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_CR0_READ_SHADOW, CR0_PG);  if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_CR4_READ_SHADOW,  0);      if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_CR3_TARGET_COUNT, 0);      if (status) return status;

	//CR GUEST
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_CR0, g_guestState.CR0); if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_CR3, g_guestState.CR3); if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_CR4, g_guestState.CR4); if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_DR7, 0x400 | DR7_GD);   if (status) return status;

	//CR HOST
	status = __vmx_vmwrite(VMX_VMCS_HOST_CR0, g_guestState.CR0);    if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_HOST_CR3, g_guestState.CR3);    if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_HOST_CR4, g_guestState.CR4);    if (status) return status;

	return status;
}

UCHAR 
SetControls()
{
	UCHAR status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_PIN_EXEC_CONTROLS,  g_guestState.PIN);   if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_PROC_EXEC_CONTROLS, g_guestState.PROC);  if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_EXIT_CONTROLS,      g_guestState.EXIT);  if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_CTRL_ENTRY_CONTROLS,     g_guestState.ENTRY); if (status) return status;
	return status;
}

UCHAR 
SetDT()
{
	UCHAR status;
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_IDTR_BASE,  g_guestState.Idtr.base);  if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS32_GUEST_IDTR_LIMIT, g_guestState.Idtr.limit); if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS64_GUEST_GDTR_BASE,  g_guestState.Gdtr.base);  if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS32_GUEST_GDTR_LIMIT, g_guestState.Gdtr.limit); if (status) return status;

	SEGMENT_SELECTOR seg_sel;
	GetSegmentDescriptor((SEGMENT_SELECTOR *)&seg_sel, g_guestState.Tr);
	status = __vmx_vmwrite(VMX_VMCS_HOST_TR_BASE, seg_sel.base);             if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_HOST_GDTR_BASE, g_guestState.Gdtr.base); if (status) return status;
	status = __vmx_vmwrite(VMX_VMCS_HOST_IDTR_BASE, g_guestState.Idtr.base); if (status) return status;
	return status;
}

struct CS_STAR
{
    union
    {
        ULONG_PTR Value;
        struct
        {
            ULONG_PTR Reserved :0x20;
            ULONG_PTR SyscallCs:0x10;
            ULONG_PTR SysretCs :0x10;
        };
    };
};

UCHAR
SetSysCall()
{
	UCHAR status;
	CS_STAR cs = { __readmsr(IA32_STAR) };

    status = __vmx_vmwrite(VMX_VMCS32_GUEST_SYSENTER_CS,  cs.SyscallCs & QWORD_LIMIT)    if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS64_GUEST_SYSENTER_ESP, (ULONG_PTR)g_guestState.SESP)  if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS64_GUEST_SYSENTER_EIP, (ULONG_PTR)g_guestState.SEIP)  if (status) return status;

    status = __vmx_vmwrite(VMX_VMCS32_HOST_SYSENTER_CS,   cs.SyscallCs & QWORD_LIMIT)    if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS_HOST_SYSENTER_EIP,    (ULONG_PTR)g_guestState.SEIP)  if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS_HOST_SYSENTER_ESP,    (ULONG_PTR)g_guestState.SESP)  if (status) return status;

	return status;
}

void 
GetSegmentDescriptor( 
	__out SEGMENT_SELECTOR* segSel, 
	__in ULONG_PTR selector 
	)
{
	SEGMENT_DESCRIPTOR* seg = (SEGMENT_DESCRIPTOR *)((PUCHAR)g_guestState.Gdtr.base + (selector >> 3) * 8);	
    RtlZeroMemory(segSel, sizeof(SEGMENT_SELECTOR));

	segSel->selector = selector;
	segSel->limit =	(ULONG)(seg->LimitLow | (seg->LimitHigh << 16));
	segSel->base = seg->BaseLow | (seg->BaseMid << 16) | (seg->BaseHigh << 24);
	segSel->attributes = (USHORT)(seg->AttributesLow | (seg->AttributesHigh << 8));

	//is TSS or HV_CALLBACK ?
	if (!(seg->AttributesLow & NORMAL))
		segSel->base = segSel->base | ((*(PULONG64) ((PUCHAR)seg + 8)) << 32);

	if (segSel->attributes >> IS_GRANULARITY_4KB == 1)
		segSel->limit = (segSel->limit << 12) | 0xFFFF;

	segSel->rights = (segSel->selector ? (((PUCHAR) &segSel->attributes)[0] + (((PUCHAR) &segSel->attributes)[1] << 12)) : 0x10000);
}

UCHAR
SetSegSelector(  // done!
	__in ULONG_PTR Selector,
	__in ULONG_PTR VMCS_Index
	)
{
	UCHAR status;
	size_t i = (VMCS_Index - VMX_VMCS16_GUEST_FIELD_ES);

	SEGMENT_SELECTOR seg_sel;
	GetSegmentDescriptor(&seg_sel, Selector);

    status = __vmx_vmwrite(VMX_VMCS16_GUEST_FIELD_ES         + i, Selector)        if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS64_GUEST_ES_BASE          + i, seg_sel.base)    if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS32_GUEST_ES_LIMIT         + i, seg_sel.limit)   if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS + i, seg_sel.rights)  if (status) return status;
	
	return status;
}

UCHAR
SetSegSelectors()
{
    UCHAR status;

    //SELECTORS
    status = SetSegSelector(g_guestState.Cs, VMX_VMCS16_GUEST_FIELD_CS)     if (status) return status;
    status = SetSegSelector(g_guestState.Ds, VMX_VMCS16_GUEST_FIELD_DS)     if (status) return status;
    status = SetSegSelector(g_guestState.Es, VMX_VMCS16_GUEST_FIELD_ES)     if (status) return status;
    status = SetSegSelector(g_guestState.Ss, VMX_VMCS16_GUEST_FIELD_SS)     if (status) return status;
    status = SetSegSelector(g_guestState.Fs, VMX_VMCS16_GUEST_FIELD_FS)     if (status) return status;
    status = SetSegSelector(g_guestState.Gs, VMX_VMCS16_GUEST_FIELD_GS)     if (status) return status;
    status = SetSegSelector(g_guestState.Ldtr, VMX_VMCS16_GUEST_FIELD_LDTR) if (status) return status;
    status = SetSegSelector(g_guestState.Tr, VMX_VMCS16_GUEST_FIELD_TR)     if (status) return status;

    //HOST
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_CS, g_guestState.Cs)         if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_DS, SEG_DATA)                if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_ES, SEG_DATA)                if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_SS, g_guestState.Ss)         if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_FS, g_guestState.Fs & 0xf8)  if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_GS, g_guestState.Gs & 0xf8)  if (status) return status;
    status = __vmx_vmwrite(VMX_VMCS16_HOST_FIELD_TR, g_guestState.Tr)         if (status) return status;

    // VMWRITE_ERR_QUIT(VMX_VMCS_HOST_FS_BASE, __readmsr(IA32_FS_BASE) & SEG_Q_LIMIT);

    return status;
}