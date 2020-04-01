/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/
#define _RTL8192C_REDESC_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8192d_hal.h>

static u8 evm_db2percentage(s8 value)
{
	/*  */
	/*  -33dB~0dB to 0%~99% */
	/*  */
	s8 ret_val;

	ret_val = value;
	/* ret_val /= 2; */

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("EVMdbToPercentage92S Value=%d / %x\n", ret_val, ret_val));

	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val*=3;

	if (ret_val == 99)
		ret_val = 100;

	return(ret_val);
}

static s32 signal_scale_mapping(struct rtw_adapter *padapter, s32 cur_sig)
{
	s32 ret_sig;

	if (cur_sig >= 51 && cur_sig <= 100)
		ret_sig = 100;
	else if (cur_sig >= 41 && cur_sig <= 50)
		ret_sig = 80 + ((cur_sig - 40)*2);
	else if (cur_sig >= 31 && cur_sig <= 40)
		ret_sig = 66 + (cur_sig - 30);
	else if (cur_sig >= 21 && cur_sig <= 30)
		ret_sig = 54 + (cur_sig - 20);
	else if (cur_sig >= 10 && cur_sig <= 20)
		ret_sig = 42 + (((cur_sig - 10) * 2) / 3);
	else if (cur_sig >= 5 && cur_sig <= 9)
		ret_sig = 22 + (((cur_sig - 5) * 3) / 2);
	else if (cur_sig >= 1 && cur_sig <= 4)
		ret_sig = 6 + (((cur_sig - 1) * 3) / 2);
	else
		ret_sig = cur_sig;
	return ret_sig;
}

static s32  translate2dbm(u8 signal_strength_idx)
{
	s32	signal_power; /*  in dBm. */

	/*  Translate to dBm (x=0.5y-95). */
	signal_power = (s32)((signal_strength_idx + 1) >> 1);
	signal_power -= 95;

	return signal_power;
}

static void query_rx_phy_status(struct recv_frame_hdr *prframe, struct phy_stat *pphy_stat, bool bPacketMatchBSSID)
{
	struct phy_ofdm_rx_status_report_8192cd	*pOfdm_buf;
	struct phy_cck_rx_status_report_8192cd *pCck_buf;
	u8	i, max_spatial_stream, evm;
	s8	rx_pwr[4], rx_pwr_all=0;
	u8	pwdb_all;
	u32	rssi,total_rssi=0;
	u8	bcck_rate=0, rf_rx_num = 0, cck_highpwr = 0;
	struct rtw_adapter				*padapter = prframe->adapter;
	struct rx_pkt_attrib	*pattrib = &prframe->attrib;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	u8	tmp_rxsnr;
	s8	rx_snrX;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	bcck_rate=(pattrib->mcs_rate<=3? 1:0);

	/*  Record it for next packet processing */
	pattrib->rx_mimo_signal_qual[0]= -1;
	pattrib->rx_mimo_signal_qual[1]= -1;

	if (bcck_rate) /* CCK */
	{
		u8 report;

		/*  CCK Driver info Structure is not the same as OFDM packet. */
		pCck_buf = (struct phy_cck_rx_status_report_8192cd *)pphy_stat;

		/*  */
		/*  (1)Hardware does not provide RSSI for CCK */
		/*  (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive) */
		/*  */

		if (padapter->pwrctrlpriv.rf_pwrstate == rf_on)
			cck_highpwr = (u8)pHalData->bCckHighPower;
		else
			cck_highpwr = false;

		if (!cck_highpwr)
		{
			report = pCck_buf->cck_agc_rpt&0xc0;
			report = report>>6;
			switch (report)
			{
				/*  03312009 modified by cosa */
				/*  Modify the RF RNA gain value to -40, -20, -2, 14 by Jenyu's suggestion */
				/*  Note: different RF with the different RNA gain. */
				case 0x3:
					rx_pwr_all = (-46) - (pCck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x2:
					rx_pwr_all = (-26) - (pCck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x1:
					rx_pwr_all = (-12) - (pCck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x0:
					rx_pwr_all = (16) - (pCck_buf->cck_agc_rpt & 0x3e);
					break;
			}
		}
		else
		{
			report = pCck_buf->cck_agc_rpt & 0x60;
			report = report>>5;
			switch (report)
			{
				case 0x3:
					rx_pwr_all = (-46) - ((pCck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x2:
					rx_pwr_all = (-26)- ((pCck_buf->cck_agc_rpt & 0x1f)<<1);
					break;
				case 0x1:
					rx_pwr_all = (-12) - ((pCck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x0:
					rx_pwr_all = (16) - ((pCck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
			}
		}

		pwdb_all= query_rx_pwr_percentage(rx_pwr_all);
		{
			/*  CCK gain is smaller than OFDM/MCS gain, */
			/*  so we add gain diff by experiences, the val is 6 */
			pwdb_all+=6;
			if (pwdb_all > 100)
				pwdb_all = 100;
			/*  modify the offset to make the same gain index with OFDM. */
			if (pwdb_all > 34 && pwdb_all <= 42)
				pwdb_all -= 2;
			else if (pwdb_all > 26 && pwdb_all <= 34)
				pwdb_all -= 6;
			else if (pwdb_all > 14 && pwdb_all <= 26)
				pwdb_all -= 8;
			else if (pwdb_all > 4 && pwdb_all <= 14)
				pwdb_all -= 4;
		}

		pattrib->rxpwdb_all = pwdb_all;
		pattrib->recvsignalpower = rx_pwr_all;
		padapter->recvpriv.rxpwdb = rx_pwr_all;

		/*  */
		/*  (3) Get Signal Quality (EVM) */
		/*  */
		if (bPacketMatchBSSID)
		{
			u8	sq;

			if (pHalData->CustomerID == RT_CID_819x_Lenovo)
			{
				/*  mapping to 5 bars for vista signal strength */
				/*  signal quality in driver will be displayed to signal strength */
				/*  in vista. */
				if (pwdb_all >= 50)
					sq = 100;
				else if (pwdb_all >= 35 && pwdb_all < 50)
					sq = 80;
				else if (pwdb_all >= 22 && pwdb_all < 35)
					sq = 60;
				else if (pwdb_all >= 18 && pwdb_all < 22)
					sq = 40;
				else
					sq = 20;
			}
			else
			{
				if (pwdb_all> 40)
				{
					sq = 100;
				}
				else
				{
					sq = pCck_buf->SQ_rpt;

					if (pCck_buf->SQ_rpt > 64)
						sq = 0;
					else if (pCck_buf->SQ_rpt < 20)
						sq= 100;
					else
						sq = ((64-sq) * 100) / 44;

				}
			}

			pattrib->signal_qual=sq;
			pattrib->rx_mimo_signal_qual[0]=sq;
			pattrib->rx_mimo_signal_qual[1]=(-1);
		}
	}
	else /* OFDM/HT */
	{
		pOfdm_buf = (struct phy_ofdm_rx_status_report_8192cd *)pphy_stat;

		/*  */
		/*  (1)Get RSSI for HT rate */
		/*  */
		for (i=0; i<pHalData->NumTotalRFPath; i++)
		{
			/*  2008/01/30 MH we will judge RF RX path now. */
			if (pHalData->bRFPathRxEnable[i])
				rf_rx_num++;

			rx_pwr[i] = ((pOfdm_buf->trsw_gain_X[+i]&0x3F)*2) - 110;
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			{
				if (bPacketMatchBSSID)
					padapter->recvpriv.RxRssi[i] = rx_pwr[i];
			}
			else
				padapter->recvpriv.RxRssi[i] = rx_pwr[i];
			/* Translate DBM to percentage. */
			pattrib->rx_rssi[i] = rssi = query_rx_pwr_percentage(rx_pwr[i]);
			total_rssi += rssi;

			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], rssi));

			/* Get Rx snr value in DB */
			tmp_rxsnr = pOfdm_buf->rxsnr_X[i];
			rx_snrX = (s8)(tmp_rxsnr);
			rx_snrX >>= 1;
			padapter->recvpriv.RxSNRdB[i] =  (int)rx_snrX;

			/* Record Signal Strength for next packet */
			if (bPacketMatchBSSID)
			{

				/* The following is for lenovo signal strength in vista */
				if (pHalData->CustomerID == RT_CID_819x_Lenovo)
				{
					u8	SQ;

					if (i == 0)
					{
						/*  mapping to 5 bars for vista signal strength */
						/*  signal quality in driver will be displayed to signal strength */
						/*  in vista. */
						if (rssi >= 50)
							SQ = 100;
						else if (rssi >= 35 && rssi < 50)
							SQ = 80;
						else if (rssi >= 22 && rssi < 35)
							SQ = 60;
						else if (rssi >= 18 && rssi < 22)
							SQ = 40;
						else
							SQ = 20;
						/* DbgPrint("ofdm/mcs RSSI=%d\n", RSSI); */
						pattrib->signal_qual = SQ;
						/* DbgPrint("ofdm/mcs SQ = %d\n", pRfd->Status.SignalQuality); */
					}
				}
			}
		}

		/*  */
		/*  (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive) */
		/*  */
		rx_pwr_all = (((pOfdm_buf->pwdb_all) >> 1)& 0x7f) -106;
		pwdb_all = query_rx_pwr_percentage(rx_pwr_all);

		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("PWDB_ALL=%d\n",	pwdb_all));

		pattrib->rxpwdb_all = pwdb_all;	/* for DIG/rate adaptive */
		pattrib->recvsignalpower = rx_pwr_all;
		padapter->recvpriv.rxpwdb = rx_pwr_all;

		/*  */
		/*  (3)EVM of HT rate */
		/*  */
		if (pHalData->CustomerID != RT_CID_819x_Lenovo)
		{
			if (pattrib->rxht &&  pattrib->mcs_rate >=20 && pattrib->mcs_rate<=27)
				max_spatial_stream = 2; /* both spatial stream make sense */
			else
				max_spatial_stream = 1; /* only spatial stream 1 makes sense */

			for (i=0; i<max_spatial_stream; i++)
			{
				/*  Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment */
				/*  fill most significant bit to "zero" when doing shifting operation which may change a negative */
				/*  value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore. */
				evm = evm_db2percentage((pOfdm_buf->rxevm_X[i] /*/ 2*/));/* dbm */

				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("RXRATE=%x RXEVM=%x EVM=%s%d\n",
					pattrib->mcs_rate, pOfdm_buf->rxevm_X[i], "%",evm));

				if (bPacketMatchBSSID)
				{
					if (i==0) /*  Fill value in RFD, Get the first spatial stream only */
					{
						pattrib->signal_qual = (u8)(evm & 0xff);
					}
					pattrib->rx_mimo_signal_qual[i] = (u8)(evm & 0xff);
				}
			}

		}

		/*  */
		/*  4. Record rx statistics for debug */
		/*  */

	}

	/* UI BSS List signal strength(in percentage), make it good looking, from 0~100. */
	/* It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp(). */
	if (bcck_rate)
	{
		pattrib->signal_strength=(u8)signal_scale_mapping(padapter, pwdb_all);
	}
	else
	{
		if (rf_rx_num != 0)
		{
			pattrib->signal_strength= (u8)(signal_scale_mapping(padapter, total_rssi/=rf_rx_num));
		}
	}
}

static void process_rssi(struct rtw_adapter *padapter,struct recv_frame_hdr *prframe)
{
	u32	last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct signal_stat * signal_stat = &padapter->recvpriv.signal_strength_data;

	{
		if (signal_stat->update_req) {
			signal_stat->total_num = 0;
			signal_stat->total_val = 0;
			signal_stat->update_req = 0;
		}

		signal_stat->total_num++;
		signal_stat->total_val  += pattrib->signal_strength;
		signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
	}
}/*  Process_UI_RSSI_8192S */

static void process_PWDB(struct rtw_adapter *padapter, struct recv_frame_hdr *prframe)
{
	int	UndecoratedSmoothedPWDB;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	struct rx_pkt_attrib	*pattrib= &prframe->attrib;
	struct sta_info		*psta = prframe->psta;

	if (psta)
	{
		UndecoratedSmoothedPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;
	}
	else
	{
		UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
	}

	{
		if (UndecoratedSmoothedPWDB == 0) /*  initialize */
		{
			UndecoratedSmoothedPWDB = pattrib->rxpwdb_all;
		}

		if (pattrib->rxpwdb_all > (u32)UndecoratedSmoothedPWDB)
		{
			UndecoratedSmoothedPWDB =
					(((UndecoratedSmoothedPWDB)*(Rx_Smooth_Factor-1)) +
					(pattrib->rxpwdb_all)) /(Rx_Smooth_Factor);

			UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB + 1;
		}
		else
		{
			UndecoratedSmoothedPWDB =
					(((UndecoratedSmoothedPWDB)*(Rx_Smooth_Factor-1)) +
					(pattrib->rxpwdb_all)) /(Rx_Smooth_Factor);
		}

		if (psta)
		{
			psta->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
			pdmpriv->UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
		}
		else
		{
			pdmpriv->UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
		}
	}
}

static void process_link_qual(struct rtw_adapter *padapter,struct recv_frame_hdr *prframe)
{
	u32	last_evm=0,  tmpVal;
	struct rx_pkt_attrib *pattrib;
	struct signal_stat * signal_stat;

	if (prframe == NULL || padapter==NULL) {
		return;
	}

	pattrib = &prframe->attrib;
	signal_stat = &padapter->recvpriv.signal_qual_data;

	if (signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->signal_qual;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;

}/*  Process_UiLinkQuality8192S */

static void process_phy_info(struct rtw_adapter *padapter, struct recv_frame_hdr *prframe)
{
	struct recv_frame_hdr *precvframe = (struct recv_frame_hdr *)prframe;

	/*  */
	/*  Check RSSI */
	/*  */
	process_rssi(padapter, precvframe);
	/*  */
	/*  Check PWDB. */
	/*  */
	process_PWDB(padapter, precvframe);
	/*  */
	/*  Check EVM */
	/*  */
	process_link_qual(padapter,  precvframe);
}

void rtl8192d_translate_rx_signal_stuff(struct recv_frame_hdr *precvframe, struct phy_stat *pphy_info)
{
	struct rx_pkt_attrib	*pattrib = &precvframe->attrib;
	struct rtw_adapter				*padapter = precvframe->adapter;
	u8	bPacketMatchBSSID =false;
	u8	bPacketToSelf = false;
	u8	bPacketBeacon = false;

	if ((pattrib->physt) && (pphy_info != NULL))
	{
		bPacketMatchBSSID = ((!IsFrameTypeCtrl(precvframe->rx_data)) && !(pattrib->icv_err) && !(pattrib->crc_err) &&
			_rtw_memcmp(get_hdr_bssid(precvframe->rx_data), get_my_bssid(&padapter->mlmeextpriv.mlmext_info.network), ETH_ALEN));

		bPacketToSelf = bPacketMatchBSSID &&  (_rtw_memcmp(get_da(precvframe->rx_data), myid(&padapter->eeprompriv), ETH_ALEN));

		bPacketBeacon =bPacketMatchBSSID && (GetFrameSubType(precvframe->rx_data) ==  WIFI_BEACON);

		query_rx_phy_status(precvframe, pphy_info, bPacketMatchBSSID);

		precvframe->psta = NULL;
		if (bPacketMatchBSSID && check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)
		{
			u8 *sa;
			struct sta_info *psta=NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;

			sa = get_sa(precvframe->rx_data);

			psta = rtw_get_stainfo(pstapriv, sa);
			if (psta)
			{
				precvframe->psta = psta;
				process_phy_info(padapter, precvframe);
			}
		}
		else if (bPacketToSelf || bPacketBeacon)
		{
			if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true)
			{
				u8 *sa;
				struct sta_info *psta=NULL;
				struct sta_priv *pstapriv = &padapter->stapriv;

				sa = get_sa(precvframe->rx_data);

				psta = rtw_get_stainfo(pstapriv, sa);
				if (psta)
				{
					precvframe->psta = psta;
				}
			}

			process_phy_info(padapter, precvframe);
		}
	}
}

void rtl8192d_query_rx_desc_status(struct recv_frame_hdr *precvframe, struct recv_stat *pdesc)
{
	struct rx_pkt_attrib	*pattrib = &precvframe->attrib;

	/* Offset 0 */
	pattrib->physt = (u8)((le32_to_cpu(pdesc->rxdw0) >> 26) & 0x1);
	pattrib->pkt_len =  (u16)(le32_to_cpu(pdesc->rxdw0)&0x00003fff);
	pattrib->drvinfo_sz = (u8)((le32_to_cpu(pdesc->rxdw0) >> 16) & 0xf) * 8;/* uint 2^3 = 8 bytes */

	pattrib->shift_sz = (u8)((le32_to_cpu(pdesc->rxdw0) >> 24) & 0x3);

	pattrib->crc_err = (u8)((le32_to_cpu(pdesc->rxdw0) >> 14) & 0x1);
	pattrib->icv_err = (u8)((le32_to_cpu(pdesc->rxdw0) >> 15) & 0x1);
	pattrib->qos = (u8)((le32_to_cpu(pdesc->rxdw0) >> 23) & 0x1);/*  Qos data, wireless lan header length is 26 */
	pattrib->bdecrypted = (le32_to_cpu(pdesc->rxdw0) & BIT(27))? 0:1;

	/* Offset 4 */
	pattrib->mfrag = (u8)((le32_to_cpu(pdesc->rxdw1) >> 27) & 0x1);/* more fragment bit */

	/* Offset 8 */
	pattrib->frag_num = (u8)((le32_to_cpu(pdesc->rxdw2) >> 12) & 0xf);/* fragmentation number */

	/* Offset 12 */
#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	if (le32_to_cpu(pdesc->rxdw3) & BIT(13)) {
		pattrib->tcpchk_valid = 1; /*  valid */
		if (le32_to_cpu(pdesc->rxdw3) & BIT(11)) {
			pattrib->tcp_chkrpt = 1; /*  correct */
		}
		else
			pattrib->tcp_chkrpt = 0; /*  incorrect */

		if (le32_to_cpu(pdesc->rxdw3) & BIT(12))
			pattrib->ip_chkrpt = 1; /*  correct */
		else
			pattrib->ip_chkrpt = 0; /*  incorrect */
	}
	else {
		pattrib->tcpchk_valid = 0; /*  invalid */
	}
#endif

	pattrib->mcs_rate=(u8)((le32_to_cpu(pdesc->rxdw3))&0x3f);
	pattrib->rxht=(u8)((le32_to_cpu(pdesc->rxdw3) >>6)&0x1);

	/* Offset 16 */
	/* Offset 20 */
}
