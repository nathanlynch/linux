# SPDX-License-Identifier: GPL-2.0

mpic-msi-obj-$(CONFIG_PCI_MSI)	+= mpic_msi.o mpic_u3msi.o
obj-$(CONFIG_MPIC)		+= mpic.o $(mpic-msi-obj-y)
obj-$(CONFIG_MPIC_TIMER)        += mpic_timer.o
obj-$(CONFIG_FSL_MPIC_TIMER_WAKEUP)	+= fsl_mpic_timer_wakeup.o
mpic-msgr-obj-$(CONFIG_MPIC_MSGR)	+= mpic_msgr.o
obj-$(CONFIG_MPIC)		+= mpic.o $(mpic-msi-obj-y) $(mpic-msgr-obj-y)
obj-$(CONFIG_PPC_EPAPR_HV_PIC)	+= ehv_pic.o
fsl-msi-obj-$(CONFIG_PCI_MSI)	+= fsl_msi.o
obj-$(CONFIG_PPC_MSI_BITMAP)	+= msi_bitmap.o

obj-$(CONFIG_PPC_MPC106)	+= grackle.o
obj-$(CONFIG_PPC_DCR_NATIVE)	+= dcr-low.o
obj-$(CONFIG_U3_DART)		+= dart_iommu.o
obj-$(CONFIG_MMIO_NVRAM)	+= mmio_nvram.o
obj-$(CONFIG_FSL_SOC)		+= fsl_soc.o fsl_mpic_err.o
obj-$(CONFIG_FSL_PCI)		+= fsl_pci.o $(fsl-msi-obj-y)
obj-$(CONFIG_FSL_PMC)		+= fsl_pmc.o
obj-$(CONFIG_FSL_CORENET_RCPM)	+= fsl_rcpm.o
obj-$(CONFIG_FSL_LBC)		+= fsl_lbc.o
obj-$(CONFIG_FSL_GTM)		+= fsl_gtm.o
obj-$(CONFIG_FSL_RIO)		+= fsl_rio.o fsl_rmu.o
obj-$(CONFIG_TSI108_BRIDGE)	+= tsi108_pci.o tsi108_dev.o
obj-$(CONFIG_RTC_DRV_CMOS)	+= rtc_cmos_setup.o

obj-$(CONFIG_PPC_INDIRECT_PCI)	+= indirect_pci.o
obj-$(CONFIG_PPC_I8259)		+= i8259.o
obj-$(CONFIG_IPIC)		+= ipic.o
obj-$(CONFIG_OF_RTC)		+= of_rtc.o

obj-$(CONFIG_CPM)		+= cpm_common.o
obj-$(CONFIG_CPM2)		+= cpm2.o cpm2_pic.o cpm_gpio.o
obj-$(CONFIG_8xx_GPIO)		+= cpm_gpio.o
obj-$(CONFIG_QUICC_ENGINE)	+= cpm_common.o
obj-$(CONFIG_PPC_DCR)		+= dcr.o

obj-$(CONFIG_PPC_MPC512x)	+= mpc5xxx_clocks.o
obj-$(CONFIG_PPC_MPC52xx)	+= mpc5xxx_clocks.o

ifdef CONFIG_SUSPEND
obj-$(CONFIG_PPC_BOOK3S_32)	+= 6xx-suspend.o
endif

obj-$(CONFIG_PPC_EARLY_DEBUG_MEMCONS)	+= udbg_memcons.o

obj-$(CONFIG_PPC_XICS)		+= xics/
obj-$(CONFIG_PPC_XIVE)		+= xive/

obj-$(CONFIG_GE_FPGA)		+= ge/
