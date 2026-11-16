	.section __DATA,__const
	.globl _fw_yc_me, _fw_yc_me_end
	.p2align 8
_fw_yc_me:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_me.bin", 256, 263168
_fw_yc_me_end:
	.globl _fw_yc_pfp, _fw_yc_pfp_end
	.p2align 8
_fw_yc_pfp:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_pfp.bin", 256, 263168
_fw_yc_pfp_end:
	.globl _fw_yc_ce, _fw_yc_ce_end
	.p2align 8
_fw_yc_ce:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_ce.bin", 256, 263040
_fw_yc_ce_end:
	.globl _fw_yc_mec, _fw_yc_mec_end
	.p2align 8
_fw_yc_mec:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_mec.bin", 256, 267440
_fw_yc_mec_end:
	.globl _fw_yc_mec_jt, _fw_yc_mec_jt_end
	.p2align 8
_fw_yc_mec_jt:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_mec.bin", 267696, 896
_fw_yc_mec_jt_end:
	.globl _fw_yc_rlc, _fw_yc_rlc_end
	.p2align 8
_fw_yc_rlc:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 256, 25088
_fw_yc_rlc_end:
	.globl _fw_yc_srlist_cntl, _fw_yc_srlist_cntl_end
	.p2align 8
_fw_yc_srlist_cntl:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 25344, 592
_fw_yc_srlist_cntl_end:
	.globl _fw_yc_srlist_gpm, _fw_yc_srlist_gpm_end
	.p2align 8
_fw_yc_srlist_gpm:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 25936, 1536
_fw_yc_srlist_gpm_end:
	.globl _fw_yc_srlist_srm, _fw_yc_srlist_srm_end
	.p2align 8
_fw_yc_srlist_srm:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 27472, 18560
_fw_yc_srlist_srm_end:
	.globl _fw_yc_rlc_iram, _fw_yc_rlc_iram_end
	.p2align 8
_fw_yc_rlc_iram:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 46032, 66048
_fw_yc_rlc_iram_end:
	.globl _fw_yc_rlc_dram, _fw_yc_rlc_dram_end
	.p2align 8
_fw_yc_rlc_dram:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_rlc.bin", 112080, 66048
_fw_yc_rlc_dram_end:
	.globl _fw_yc_sdma, _fw_yc_sdma_end
	.p2align 8
_fw_yc_sdma:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_sdma.bin", 256, 33792
_fw_yc_sdma_end:
	.globl _fw_yc_toc, _fw_yc_toc_end
	.p2align 8
_fw_yc_toc:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_toc.bin", 256, 1536
_fw_yc_toc_end:
	.globl _fw_yc_dmcub, _fw_yc_dmcub_end
	.p2align 8
_fw_yc_dmcub:
	.incbin "src/Hardware/Rembrandt/Firmware/yellow_carp_dmcub.bin", 256, 231440
_fw_yc_dmcub_end:
