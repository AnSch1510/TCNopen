/**********************************************************************************************************************/
/**
 * @file            tau_xsession.h
 *
 * @brief           Utility functions to simplify boilerplate code for an application based on an XML configuration.
 *
 * @details         tau_xsession uses the xml-config feature and provides "easy" abstraction accessing telegrams and
 *                  setting up a simple cycle.
 *                  The library also circumvents the trdp-xml-mem-config-chicken-and-egg-issue, ie., trdp-xml uses the
 *                  vos-mem subsystem, but the read xml-config may include directives to configure this subsystem. See
 *                  @see tau_xsession_load() for the work-around-approach.
 *
 *                  Initial routines and concepts were copied from the XML example of TRDP.
 *
 *
 * @note            Project: TCNOpen TRDP prototype stack
 *
 * @author          Thorsten Schulz
 *
 * @remarks This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 *          If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *          Copyright 2019 University of Rostock
 *
 * $Id$
 */

#include "tau_xsession.h"
#include "tau_marshall.h"
#include "vos_utils.h"
#include "trdp_xml.h"
#include <string.h>
#include <strings.h>

#include "tau_xmarshall.h"

/* static members */
static struct xsession_common {

	INT32                  use;
	TAU_XSESSION_PRINT     app_cput;
	TAU_XSESSION_T        *session;

/* needed for load & init */
	UINT32                 numIfConfig;
	TRDP_IF_CONFIG_T       ifConfig[MAX_INTERFACES];
	UINT32                 numComPar;
	TRDP_COM_PAR_T         comPar[MAX_COMPAR];
	TRDP_XML_DOC_HANDLE_T  devDocHnd;

/*  Log configuration   */
	TRDP_DBG_CONFIG_T      dbgConfig;
	INT32                  maxLogCategory;

/*  Marshalling configuration initialized from datasets defined in xml  */
/*  currently our is static to all sessions */
	TRDP_MARSHALL_CONFIG_T marshallCfg;

/*  Dataset configuration from xml configuration file */
	UINT32                 numComId;
	TRDP_COMID_DSID_MAP_T *pComIdDsIdMap;
	UINT32                 numDataset;
	apTRDP_DATASET_T       apDataset;

} _ = {-1, };

/* protected */
static TRDP_ERR_T initMarshalling(const TRDP_XML_DOC_HANDLE_T * pDocHnd);
static TRDP_ERR_T findDataset(UINT32 datasetId, TRDP_DATASET_T **pDatasetDesc);

static TRDP_ERR_T publishTelegram(TAU_XSESSION_T *our, TRDP_EXCHG_PAR_T * pExchgPar, UINT32 *pubTelID, const UINT8 *data, UINT32 memLength, const TRDP_PD_INFO_T *info);
static TRDP_ERR_T subscribeTelegram(TAU_XSESSION_T *our, TRDP_EXCHG_PAR_T * pExchgPar, UINT32 *subTelID /*can be NULL*/, TRDP_PD_CALLBACK_T cb);
static TRDP_ERR_T configureSession(TAU_XSESSION_T *our, TRDP_XML_DOC_HANDLE_T *pDocHnd, void *callbackRef);


/*********************************************************************************************************************/
/** Convert provided TRDP error code to string
 */
const char *tau_getResultString(TRDP_ERR_T ret) {
	static char resStr[] = "unknown error:     ";
	switch (ret) {
	case TRDP_NO_ERR:
		return "TRDP_NO_ERR (no error)";
	case TRDP_PARAM_ERR:
		return "TRDP_PARAM_ERR (parameter missing or out of range)";
	case TRDP_INIT_ERR:
		return "TRDP_INIT_ERR (call without valid initialization)";
	case TRDP_NOINIT_ERR:
		return "TRDP_NOINIT_ERR (call with invalid handle)";
	case TRDP_TIMEOUT_ERR:
		return "TRDP_TIMEOUT_ERR (timeout)";
	case TRDP_NODATA_ERR:
		return "TRDP_NODATA_ERR (non blocking mode: no data received)";
	case TRDP_SOCK_ERR:
		return "TRDP_SOCK_ERR (socket error / option not supported)";
	case TRDP_IO_ERR:
		return "TRDP_IO_ERR (socket IO error, data can't be received/sent)";
	case TRDP_MEM_ERR:
		return "TRDP_MEM_ERR (no more memory available)";
	case TRDP_SEMA_ERR:
		return "TRDP_SEMA_ERR semaphore not available)";
	case TRDP_QUEUE_ERR:
		return "TRDP_QUEUE_ERR (queue empty)";
	case TRDP_QUEUE_FULL_ERR:
		return "TRDP_QUEUE_FULL_ERR (queue full)";
	case TRDP_MUTEX_ERR:
		return "TRDP_MUTEX_ERR (mutex not available)";
	case TRDP_NOSESSION_ERR:
		return "TRDP_NOSESSION_ERR (no such session)";
	case TRDP_SESSION_ABORT_ERR:
		return "TRDP_SESSION_ABORT_ERR (Session aborted)";
	case TRDP_NOSUB_ERR:
		return "TRDP_NOSUB_ERR (no subscriber)";
	case TRDP_NOPUB_ERR:
		return "TRDP_NOPUB_ERR (no publisher)";
	case TRDP_NOLIST_ERR:
		return "TRDP_NOLIST_ERR (no listener)";
	case TRDP_CRC_ERR:
		return "TRDP_CRC_ERR (wrong CRC)";
	case TRDP_WIRE_ERR:
		return "TRDP_WIRE_ERR (wire error)";
	case TRDP_TOPO_ERR:
		return "TRDP_TOPO_ERR (invalid topo count)";
	case TRDP_COMID_ERR:
		return "TRDP_COMID_ERR (unknown comid)";
	case TRDP_STATE_ERR:
		return "TRDP_STATE_ERR (call in wrong state)";
	case TRDP_APP_TIMEOUT_ERR:
		return "TRDP_APPTIMEOUT_ERR (application timeout)";
	case TRDP_MARSHALLING_ERR:
		return "TRDP_MARSHALLING_ERR (alignment problem)";
	case TRDP_BLOCK_ERR:
    	return "System call would have blocked in blocking mode";
	case TRDP_UNKNOWN_ERR:
		return "TRDP_UNKNOWN_ERR (unspecified error)";
	default:
		snprintf(resStr, sizeof(resStr), "unknown error: %d", ret);
		return resStr;
	}
}

/*********************************************************************************************************************/
/** callback routine for TRDP logging/error output
 *
 *  @param[in]      pRefCon            user supplied context pointer
 *  @param[in]        category        Log category (Error, Warning, Info etc.)
 *  @param[in]        pTime            pointer to NULL-terminated string of time stamp
 *  @param[in]        pFile            pointer to NULL-terminated string of source module
 *  @param[in]        LineNumber        line
 *  @param[in]        pMsgStr         pointer to NULL-terminated string
 *  @retval         none
 */
static void dbgOut (void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime, const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr) {
	static const char *catStr[] = {"**Error: ", "Warning: ", "   Info: ", "  Debug: "};

	/*  Check message category*/
	if (!_.app_cput || (INT32)category > _.maxLogCategory) return;

	/* chop the duplicate line break */
	char putNL = 1;
	if (pMsgStr && *pMsgStr) {
		const char *p = pMsgStr;
		do { p++; } while (*p);
		if (*(--p) == '\n') putNL=0;
	}

	/*  Log message */
	if (pRefCon) {
		char str[1024];
		vos_snprintf(str, sizeof(str), "%s-%s%s:%u: ",
			/* time */		(_.dbgConfig.option & TRDP_DBG_TIME) ? pTime:"",
			/* category */	(_.dbgConfig.option & TRDP_DBG_CAT) ? catStr[category]:"",
			/* location */	(_.dbgConfig.option & TRDP_DBG_LOC) ? pFile:"",
							(_.dbgConfig.option & TRDP_DBG_LOC) ? LineNumber:0);
		_.app_cput(str, pMsgStr, putNL);
	} else
		_.app_cput("DBG: ", pMsgStr, putNL);
}

/*********************************************************************************************************************/
/** Parse dataset configuration
 *  Initialize marshalling
 */
static TRDP_ERR_T initMarshalling(const TRDP_XML_DOC_HANDLE_T * pDocHnd) {
	TRDP_ERR_T result;

	/*  Read dataset configuration  */
	result = tau_readXmlDatasetConfig(pDocHnd, &_.numComId, &_.pComIdDsIdMap, &_.numDataset, &_.apDataset);
	if (result != TRDP_NO_ERR) {
		vos_printLog(VOS_LOG_ERROR, "Failed to read dataset configuration: ""%s", tau_getResultString(result));
		return result;
	}

	/*  Initialize marshalling  */
	int xmap_valid = 1;
	for (int i=1; i<19 && xmap_valid; i++) {
		if ( !__TAU_XTYPE_MAP[i] || !__TAU_XTYPE_MAP[i+20] ) xmap_valid = 0;
	}
	/* basically, take values, sort the arrays, but takes no copy! */
	if (xmap_valid) {
		result = tau_xinitMarshall(NULL /*cur. a nop*/, _.numComId, _.pComIdDsIdMap, _.numDataset, _.apDataset);
		vos_printLog(VOS_LOG_INFO, "Using EXTENDED marshalling.");
	} else {
		result = tau_initMarshall( NULL /*cur. a nop*/, _.numComId, _.pComIdDsIdMap, _.numDataset, _.apDataset);
		vos_printLog(VOS_LOG_INFO, "Using default marshalling.");
	}
	if (result != TRDP_NO_ERR) {
		tau_freeXmlDatasetConfig(_.numComId, _.pComIdDsIdMap, _.numDataset, _.apDataset);
		_.numComId = 0;
		_.pComIdDsIdMap = NULL;
		_.numDataset = 0;
		_.apDataset = NULL;
		vos_printLog(VOS_LOG_ERROR, "Failed to initialize marshalling: ""%s", tau_getResultString(result));
		return result;
	}

	/*  Strore pointers to marshalling functions    */
	_.marshallCfg.pfCbMarshall   = xmap_valid ? tau_xmarshall : tau_marshall;
	_.marshallCfg.pfCbUnmarshall = xmap_valid ? tau_xunmarshall : tau_unmarshall;
	_.marshallCfg.pRefCon = NULL; /* if we overwrite with own functions, pRefCon may be set to @our or something like it */

	vos_printLog(VOS_LOG_INFO, "Initialized %cmarshalling for %d datasets, %d ComId to Dataset Id relations",
			xmap_valid?'x':' ',	_.numDataset, _.numComId);
	return TRDP_NO_ERR;
}

/*********************************************************************************************************************/
/** Search local data sets for given ID
 */
static TRDP_ERR_T findDataset(UINT32 datasetId, TRDP_DATASET_T **pDatasetDesc) {
	/*  Find data set for the ID   */
	for (UINT32 i = 0; pDatasetDesc && i < _.numDataset; i++) {
		if (_.apDataset[i] && _.apDataset[i]->id == datasetId) {
			*pDatasetDesc = _.apDataset[i];
			return TRDP_NO_ERR;
		}
	}

	return TRDP_PARAM_ERR;
}


int tau_xsession_up(TAU_XSESSION_T *our) {
	return our && our->initialized;
}

/*********************************************************************************************************************/
/** Publish telegram for each configured destination.
 *  Reference to each published telegram is stored in array of published telegrams
 *  our whole shebang doesn't work w/o an examplenary message
 */
static TRDP_ERR_T publishTelegram(TAU_XSESSION_T *our, TRDP_EXCHG_PAR_T * pExchgPar, UINT32 *pubTelID, const UINT8 *data, UINT32 memLength, const TRDP_PD_INFO_T *info) {
	UINT32 i;
	TRDP_SEND_PARAM_T *pSendParam = NULL;
	UINT32 interval = 0;
	TRDP_FLAGS_T flags;
	UINT32 redId = 0;
	TLG_T *pTlg = NULL;
	UINT32 destIP = 0;
	TRDP_ERR_T result;


	/*  Get communication parameters  */
	if (pExchgPar->comParId == 1)      pSendParam = &our->pdConfig.sendParam;
	else if (pExchgPar->comParId == 2) pSendParam = &our->mdConfig.sendParam;
	else for (i = 0; i < _.numComPar; i++)
		if (_.comPar[i].id == pExchgPar->comParId) pSendParam = &_.comPar[i].sendParam;

	if (!pSendParam) {
		vos_printLog(VOS_LOG_ERROR, "Unknown comParId %d for comID %d", pExchgPar->comParId, pExchgPar->comId);
		return TRDP_PARAM_ERR;
	}

	/*  Get interval and flags   */
	interval = our->processConfig.cycleTime;
	flags = our->pdConfig.flags;
	if (pExchgPar->pPdPar) {
		interval = pExchgPar->pPdPar->cycle;
		if (pExchgPar->pPdPar->flags != TRDP_FLAGS_DEFAULT) flags = pExchgPar->pPdPar->flags;
		redId = pExchgPar->pPdPar->redundant;
	}

	/*  Iterate over all destinations   */
	UINT32 dstcnt = pExchgPar->destCnt;
	if (!dstcnt && info) dstcnt++;

	for (i = 0; i < dstcnt; i++) {
		TRDP_DEST_T pDest = { (UINT32)(~0), NULL, NULL, NULL};
		if (i < pExchgPar->destCnt) pDest = pExchgPar->pDest[i];

		/* Get free published telegram descriptor   */
		if (our->numTelegrams >= MAX_TELEGRAMS) {
			vos_printLog(VOS_LOG_ERROR, "Maximum number of published telegrams %d exceeded", MAX_TELEGRAMS);
			return TRDP_PARAM_ERR;
		}

		/*  Convert host URI to IP address  */
		destIP = 0;

		if (pDest.pUriHost) {
			destIP = vos_dottedIP(*(pDest.pUriHost));
			if (*pDest.pUriHost && **pDest.pUriHost && !destIP) {
				vos_printLog(VOS_LOG_ERROR, "Invalid IP address %s configured for comID %d, destID %d",
						*pDest.pUriHost, pExchgPar->comId, pDest.id);
				return TRDP_PARAM_ERR;
			}
		}
		if (!destIP && info) destIP = info->replyIpAddr? info->replyIpAddr : info->srcIpAddr;

		if (interval && (destIP == 0 || destIP == 0xFFFFFFFF)) {
			vos_printLog(VOS_LOG_ERROR, "Invalid IP address %s/%x specified for comID %d, destID %d",
					*pDest.pUriHost, destIP, pExchgPar->comId, pDest.id);
			return TRDP_PARAM_ERR;
		}

		/*  Publish the telegram    */
		/* setting the data-pointer to NULL here will avoid early sending */
		/* for variable sized datasets, I am in trouble, because I need to set their length based on data */
		TRDP_PUB_T pHnd;
		result = tlp_publish(
				our->sessionhandle, &pHnd, NULL /* user ref for filler */, NULL /* set data filler here*/,
				pExchgPar->comId,
				0, 0, 0, destIP, interval, redId, flags, pSendParam,
				data, memLength);

		if (result != TRDP_NO_ERR) {
			vos_printLog(VOS_LOG_ERROR, "tlp_publish for comID %d, destID %d failed: %s",
					pExchgPar->comId, pDest.id, tau_getResultString(result));
			return result;
		} else {
			vos_printLog(VOS_LOG_INFO, "Published telegram: ComId %d, DestId %d", pExchgPar->comId, pDest.id);

			/*  Initialize telegram descriptor  */
			pTlg = &our->aTelegrams[our->numTelegrams];
			if (pubTelID) *pubTelID++ = our->numTelegrams;
			our->numTelegrams++;
			pTlg->comID = pExchgPar->comId;
			pTlg->peerID = pDest.id;
			pTlg->handle = pHnd;
		}
	}
	/* Also check if we need to subscribe to requests. */
	/* TODO There maybe unexpected behaviour for mixed configurations. Revise some time. */
	if (!interval) subscribeTelegram(our, pExchgPar, NULL, NULL);

	return TRDP_NO_ERR;
}

/*********************************************************************************************************************/
/** Subscribe telegram for each configured source
 *  If destination with MC address is also configured this MC address is used in the subscribe (for join)
 *  Reference to each subscribed telegram is stored in array of subscribed telegrams
 */
static TRDP_ERR_T subscribeTelegram(TAU_XSESSION_T *our, TRDP_EXCHG_PAR_T * pExchgPar, UINT32 *subTelID, TRDP_PD_CALLBACK_T cb) {
	UINT32 i;
	UINT32 timeout = 0;
	TRDP_TO_BEHAVIOR_T toBehav;
	TRDP_FLAGS_T flags;
	TLG_T *pTlg = NULL;
	UINT32 destMCIP = 0;
	UINT32 srcIP1 = 0;
	UINT32 srcIP2 = 0;
	TRDP_ERR_T result;

	/*  Get timeout, timeout behavior and flags   */
	timeout = our->pdConfig.timeout;
	toBehav = our->pdConfig.toBehavior;
	flags = our->pdConfig.flags;
	if (pExchgPar->pPdPar) {
		if (pExchgPar->pPdPar->timeout != 0) timeout = pExchgPar->pPdPar->timeout;
		if (pExchgPar->pPdPar->toBehav != TRDP_TO_DEFAULT) toBehav = pExchgPar->pPdPar->toBehav;
		if (pExchgPar->pPdPar->flags   != TRDP_FLAGS_DEFAULT) flags   = pExchgPar->pPdPar->flags;
	}
    if (cb) {
    	flags |= TRDP_FLAGS_CALLBACK;
    	flags |= TRDP_FLAGS_FORCE_CB; /* TODO, this is a work-around artifact */
    	flags &=~TRDP_FLAGS_MARSHALL; /* marshalling does not work for callback */
    }

	/*  Try to find MC destination address  */
	for (i = 0; i < pExchgPar->destCnt; i++) {
		if (pExchgPar->pDest[i].pUriHost) destMCIP = vos_dottedIP(*(pExchgPar->pDest[i].pUriHost));
		if (vos_isMulticast(destMCIP))    break;
		else                              destMCIP = 0;
	}

	/*  Iterate over all sources   */
	for (i = 0; i < pExchgPar->srcCnt; i++) {
		/* Get free subscribed telegram descriptor   */
		if (our->numTelegrams < MAX_TELEGRAMS) {
			pTlg = &our->aTelegrams[our->numTelegrams];
			if (subTelID) *subTelID++ = our->numTelegrams;
			our->numTelegrams += 1;
		} else {
			vos_printLog(VOS_LOG_ERROR, "Maximum number of subscribed telegrams %d exceeded", MAX_TELEGRAMS);
			return TRDP_PARAM_ERR;
		}
		/*  Initialize telegram descriptor  */
		pTlg->comID = pExchgPar->comId;
		pTlg->peerID = pExchgPar->pSrc[i].id;

		/*  Convert src URIs to IP address  */
		srcIP1 = 0;
		if (pExchgPar->pSrc[i].pUriHost1 && *pExchgPar->pSrc[i].pUriHost1 && **pExchgPar->pSrc[i].pUriHost1) {
			srcIP1 = vos_dottedIP(*(pExchgPar->pSrc[i].pUriHost1));
			if (!srcIP1 || srcIP1 == 0xFFFFFFFF) {
				vos_printLog(VOS_LOG_ERROR, "Invalid IP address %s specified for URI1 in comID %d, destID %d",
						*pExchgPar->pSrc[i].pUriHost1, pExchgPar->comId, pExchgPar->pSrc[i].id);
				return result;
			}
		}
		srcIP2 = 0;
		if (pExchgPar->pSrc[i].pUriHost2) {
			srcIP2 = vos_dottedIP(*(pExchgPar->pSrc[i].pUriHost2));
			if (!srcIP2 || srcIP2 == 0xFFFFFFFF) {
				vos_printLog(VOS_LOG_ERROR, "Invalid IP address %s specified for URI2 in comID %d, destID %d",
						*pExchgPar->pSrc[i].pUriHost2, pExchgPar->comId, pExchgPar->pSrc[i].id);
				return TRDP_PARAM_ERR;
			}
		}

		/*  Subscribe the telegram    */
		result = tlp_subscribe(
				our->sessionhandle, &pTlg->handle, pTlg, cb, pExchgPar->comId,
				0, 0, srcIP1, srcIP2, destMCIP, flags, timeout, toBehav);
		if (result != TRDP_NO_ERR) {
			vos_printLog(VOS_LOG_ERROR, "tlp_subscribe for comID %d, srcID %d failed: %s",
					pExchgPar->comId, pExchgPar->pSrc[i].id, tau_getResultString(result));
			return result;
		}
		vos_printLog(VOS_LOG_INFO, "Subscribed telegram: ComId %d, SrcId %d",
				pExchgPar->comId, pExchgPar->pSrc[i].id);
	}

	return TRDP_NO_ERR;
}

/*********************************************************************************************************************/
/** Initialize and configure TRDP sessions - one for each configured interface
 */
static TRDP_ERR_T configureSession(TAU_XSESSION_T *our, TRDP_XML_DOC_HANDLE_T *pDocHnd, void *callbackRef) {
	TRDP_ERR_T result;

	if (!our->pIfConfig) return TRDP_PARAM_ERR;

	vos_printLog(VOS_LOG_INFO, "Configuring session for interface %s", our->pIfConfig->ifName);
	/*  Read telegrams configured for the interface */
	result = tau_readXmlInterfaceConfig(
			pDocHnd, our->pIfConfig->ifName,
			&our->processConfig,
			&our->pdConfig, &our->mdConfig,
			&our->numExchgPar, &our->pExchgPar);
	if (result != TRDP_NO_ERR) {
		vos_printLog(VOS_LOG_ERROR, "Failed to parse configuration for interface %s: %s",
				our->pIfConfig->ifName, tau_getResultString(result));
		return result;
	}

	/*  Assure minimum cycle time    */
	our->pdConfig.pRefCon = callbackRef;

	/*  Open session for the interface  */
	result = tlc_openSession(
			&our->sessionhandle, our->pIfConfig->hostIp, our->pIfConfig->leaderIp,
			&_.marshallCfg, &our->pdConfig, &our->mdConfig, &our->processConfig);

	if (result != TRDP_NO_ERR) {
		vos_printLog(VOS_LOG_ERROR, "Failed to open session for interface %s: %s",
				our->pIfConfig->ifName, tau_getResultString(result));
		/* some clean up */
		/*  Free allocated memory - parsed telegram configuration */
		tau_freeTelegrams(our->numExchgPar, our->pExchgPar);
		our->numExchgPar = 0;
		our->pExchgPar = NULL;

		return result;
	}

	vos_printLog(VOS_LOG_INFO, "Initialized session for interface %s", our->pIfConfig->ifName);
	return TRDP_NO_ERR;
}


TRDP_ERR_T tau_xsession_load(const char *xml, size_t length, TAU_XSESSION_PRINT dbg_print) {
	TRDP_ERR_T result;

	if (_.devDocHnd.pXmlDocument || _.use >= 0) return TRDP_INIT_ERR; /* must close first */
	/*  Dataset configuration from xml configuration file */

	_.numComId = 0u;
	_.pComIdDsIdMap = NULL;
	_.numDataset = 0u;
	_.apDataset = NULL;
	_.app_cput = dbg_print;

	TRDP_IF_CONFIG_T  *pTempIfConfig;
	TRDP_COM_PAR_T    *pTempComPar;
	TRDP_MEM_CONFIG_T  tempMemConfig;
	XML_HANDLE_T       tempXML;

	result = vos_memInit(NULL, 20000, NULL);
	if (result == TRDP_NO_ERR) {
		/*  Prepare XML document    */
		result = length ? tau_prepareXmlMem(xml,  length,  &_.devDocHnd) : tau_prepareXmlDoc(xml, &_.devDocHnd);
		if (result != TRDP_NO_ERR) {
			vos_printLog(VOS_LOG_ERROR, "Failed to prepare XML document: %s", tau_getResultString(result));
		} else {

			/*  Read general parameters from XML configuration*/
			result = tau_readXmlDeviceConfig( &_.devDocHnd,
					&tempMemConfig, &_.dbgConfig,
					&_.numComPar, &pTempComPar,
					&_.numIfConfig, &pTempIfConfig);

			if (result != TRDP_NO_ERR) {
				vos_printLog(VOS_LOG_ERROR, "Failed to parse general parameters: ""%s", tau_getResultString(result));
			} else {
				if (_.numIfConfig > MAX_INTERFACES) {
					vos_printLog(VOS_LOG_ERROR, "Failed to parse general parameters: There were more interfaces available (%d) than expected (%d)",
							_.numIfConfig, MAX_INTERFACES);
					result = TRDP_PARAM_ERR;
				} else if (_.numComPar > MAX_COMPAR) {
					vos_printLog(VOS_LOG_ERROR, "Failed to parse general parameters: There were more com-parameter available (%d) than expected (%d)",
							_.numComPar, MAX_COMPAR);
					result = TRDP_PARAM_ERR;
				} else {
					if (pTempIfConfig && _.numIfConfig) memcpy(_.ifConfig, pTempIfConfig, sizeof(TRDP_IF_CONFIG_T)*_.numIfConfig); else _.numIfConfig = 0;
					if (pTempComPar   && _.numComPar  ) memcpy(_.comPar,   pTempComPar,   sizeof(TRDP_COM_PAR_T)*_.numComPar); else _.numComPar = 0;
					tempXML = *_.devDocHnd.pXmlDocument;
				}
			}
			if (result != TRDP_NO_ERR) tau_freeXmlDoc(&_.devDocHnd);
		}
		vos_memDelete(NULL); /* free above allocated memArea, as tlc_init will create a new one :/ */
	}
	if (result != TRDP_NO_ERR) return result;

	/*  Set log configuration   */
	_.dbgConfig.option |= 0xE0;
	_.dbgConfig.option &=~TRDP_DBG_DBG;
	_.maxLogCategory = -1;
	if (_.dbgConfig.option & TRDP_DBG_DBG)   _.maxLogCategory = VOS_LOG_DBG;
	else if (_.dbgConfig.option & TRDP_DBG_INFO)  _.maxLogCategory = VOS_LOG_INFO;
	else if (_.dbgConfig.option & TRDP_DBG_WARN)  _.maxLogCategory = VOS_LOG_WARNING;
	else if (_.dbgConfig.option & TRDP_DBG_ERR)   _.maxLogCategory = VOS_LOG_ERROR;


	/*  Initialize the stack    */
	result = tlc_init(dbgOut, &_.dbgConfig, &tempMemConfig);
	if (result != TRDP_NO_ERR) {
		vos_printLog(VOS_LOG_ERROR, "Failed to initialize TRDP stack: ""%s", tau_getResultString(result));
	} else {
		/* restore XML holder */
		_.devDocHnd.pXmlDocument = (XML_HANDLE_T *) vos_memAlloc(sizeof(XML_HANDLE_T));
		if (_.devDocHnd.pXmlDocument == NULL) return TRDP_MEM_ERR;
		*_.devDocHnd.pXmlDocument = tempXML;

		/*  Read dataset configuration, initialize marshalling  */
		result = initMarshalling(&_.devDocHnd);
		if (result != TRDP_NO_ERR) {
			tau_freeXmlDoc(&_.devDocHnd);
			tlc_terminate();
			_.use = -1;
		} else
			_.use = 0; /* init */
	}
	return result;
}

TRDP_ERR_T tau_xsession_init(TAU_XSESSION_T **our, const char *busInterfaceName, void *callbackRef) {
	TRDP_ERR_T result = TRDP_INIT_ERR;

	/*  Log configuration   */
	if (!_.devDocHnd.pXmlDocument || _.use < 0) {
		vos_printLog(VOS_LOG_ERROR, "XML device configuration not available.");
		return result;
	}

	TAU_XSESSION_T *s = (TAU_XSESSION_T *)vos_memAlloc( sizeof(TAU_XSESSION_T) );
	if (!s) return TRDP_MEM_ERR;

	for (UINT32 i=0; i<_.numIfConfig; i++)
		if (strcasecmp(busInterfaceName, _.ifConfig[i].ifName) == 0) {
			if ( !s->pIfConfig )
				s->pIfConfig = &_.ifConfig[i];
			else {
				vos_printLog(VOS_LOG_ERROR, "Multiple interfaces match \"%s\" in this XSession configuration.", busInterfaceName);
				vos_memFree(s);
				return result;
			}
		}

	if (s->pIfConfig) {
		/*  Initialize TRDP sessions    */
		result = configureSession(s, &_.devDocHnd, callbackRef);
		//	tlc_openSession(&appHandle, ownIpAddr, leaderIpAddr, pMarshall, pPdDefault, pMdDefault, pProcessConfig);
	} else
		vos_printLog(VOS_LOG_ERROR, "Found no interface to match \"%s\" in this XSession configuration.", busInterfaceName);
	if (result == TRDP_NO_ERR) {
		_.use++;
		s->next = _.session;
		_.session = s;
		s->initialized = _.use; /* something non-0 */
		vos_getTime ( &s->lastTime );
		if (our) *our = s;
	} else {
		vos_memFree(s);
	}
	return result;
}

TRDP_ERR_T tau_xsession_publish(TAU_XSESSION_T *our, UINT32 ComID, UINT32 *pubTelID, const UINT8 *data, UINT32 length, const TRDP_PD_INFO_T *info) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	TRDP_ERR_T result = TRDP_COMID_ERR;
	for (UINT32 tlgIdx = 0; tlgIdx < our->numExchgPar; tlgIdx++) {
		if ((our->pExchgPar[tlgIdx].destCnt || info) && our->pExchgPar[tlgIdx].comId == ComID) {
			/*  Destinations defined - publish the telegram */
			result = publishTelegram(our, &our->pExchgPar[tlgIdx], pubTelID, data, length, info);
			if (result != TRDP_NO_ERR) {
				vos_printLog(VOS_LOG_WARNING, "Failed to publish telegram comId=%d for interface %s",
						our->pExchgPar[tlgIdx].comId, our->pIfConfig->ifName);
			}
			/* our should only match one telegram */
			break;
		}
	}

	return result;
}

TRDP_ERR_T tau_xsession_subscribe(TAU_XSESSION_T *our, UINT32 ComID, UINT32 *subTelID, TRDP_PD_CALLBACK_T cb) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	TRDP_ERR_T result = TRDP_COMID_ERR;
	for (UINT32 tlgIdx = 0; tlgIdx < our->numExchgPar; tlgIdx++) {
		if (our->pExchgPar[tlgIdx].srcCnt && our->pExchgPar[tlgIdx].comId == ComID) {
			/*  Sources defined - subscribe the telegram */
			result = subscribeTelegram(our, &our->pExchgPar[tlgIdx], subTelID, cb);
			if (result != TRDP_NO_ERR) {
				vos_printLog(VOS_LOG_WARNING, "Failed to subscribe telegram comId=%d for interface %s",
						our->pExchgPar[tlgIdx].comId, our->pIfConfig->ifName);
			}
			/* our should only match one telegram */
			break;
		}
	}

	return result;
}

/*
 * Thoughts:
 * Ideally, cycle should wait for packets as long as possible, before having to return to the main application again.
 * When waiting for multiple interfaces, each inner loop should wait until one of the ifaces is due, but max for the
 * cycle period. The cycle period, however, must be provided by the caller, because it is not clear from the config for
 * multiple ifaces.
 */

TRDP_ERR_T tau_xsession_cycle_until( VOS_TIMEVAL_T deadline ) {
	TRDP_ERR_T result = TRDP_INIT_ERR;
	if ( _.use <= 0 ) return result;

	const VOS_TIMEVAL_T zero = {0,0};
	VOS_TIMEVAL_T now;
	vos_getTime( &now );

	do {
		INT32 noOfDesc = 0;
		VOS_FDS_T rfds;
		FD_ZERO(&rfds);
		VOS_TIMEVAL_T max_tv = deadline;

		vos_subTime( &max_tv, &now); /* max_tv now contains the remaining max sleep time */

		for (TAU_XSESSION_T *s = _.session; s; s = s->next ) {
			VOS_TIMEVAL_T tv;
			tlc_getInterval(s->sessionhandle, &tv, &rfds, &noOfDesc);
			if (timercmp( &tv, &zero, >) && timercmp( &tv, &max_tv, <)) max_tv = tv;
		}

		if (timercmp( &max_tv, &zero, <)) max_tv = zero;  /* max_tv must not be negative */
		int rv = vos_select( noOfDesc+1, &rfds, NULL, NULL, &max_tv);

		vos_getTime( &now );
		for (TAU_XSESSION_T *s = _.session; s; s = s->next ) {
			result = tlc_process(s->sessionhandle, &rfds, &rv);
			s->lastTime = now;
		}

	} while ( timercmp( &now, &deadline, <) );

	return result;
}

TRDP_ERR_T tau_xsession_cycle_loop( TAU_XSESSION_T *our,  INT64 *timeout_us ) {
	if (_.use <= 0 || !tau_xsession_up(our) || !timeout_us) return TRDP_INIT_ERR;

	TRDP_ERR_T result;
	const VOS_TIMEVAL_T zero = {0,0};
	VOS_TIMEVAL_T now;
	vos_getTime( &now );

	VOS_TIMEVAL_T deadline = {0, our->processConfig.cycleTime};
	vos_addTime( &deadline, &our->lastTime); /* last-deadline + cycle-period */
	if (timercmp(&deadline, &now, <=)) {
		our->lastTime = deadline; /* store the new upcoming deadline */
		deadline = now; /* don't have a future marker in the past */
	}

	INT32 noOfDesc = 0;
	VOS_FDS_T rfds;
	FD_ZERO(&rfds);
	VOS_TIMEVAL_T max_tv = deadline;

	vos_subTime( &max_tv, &now); /* max_tv now contains the remaining max sleep time */

	VOS_TIMEVAL_T tv;
	tlc_getInterval(our->sessionhandle, &tv, &rfds, &noOfDesc);
	if (timercmp( &tv, &zero, >) && timercmp( &tv, &max_tv, <)) max_tv = tv;

	if (timercmp( &max_tv, &zero, <)) max_tv = zero;  /* max_tv must not be negative */
	tv = zero;
	int rv = vos_select( noOfDesc+1, &rfds, NULL, NULL, &tv);

	result = tlc_process(our->sessionhandle, &rfds, &rv);

	*timeout_us = max_tv.tv_sec*1000000+max_tv.tv_usec;

	return result;
}

TRDP_ERR_T tau_xsession_cycle( TAU_XSESSION_T *our ) {
	TRDP_ERR_T result = TRDP_INIT_ERR;
	if ( _.use <= 0 || !tau_xsession_up(our) ) return result;

	const VOS_TIMEVAL_T zero = {0,0};
	VOS_TIMEVAL_T now;
	vos_getTime( &now );

	VOS_TIMEVAL_T deadline = {0, our->processConfig.cycleTime};
	vos_addTime( &deadline, &our->lastTime); /* last-deadline + cycle-period */
	if (timercmp(&deadline, &now, <=)) {
		our->lastTime = deadline; /* store the upcoming deadline */
		deadline = now; /* don't have a future marker in the past */
	}

	do {
		INT32 noOfDesc = 0;
		VOS_FDS_T rfds;
		FD_ZERO(&rfds);
		VOS_TIMEVAL_T max_tv = deadline;

		vos_subTime( &max_tv, &now); /* max_tv now contains the remaining max sleep time */

		for (TAU_XSESSION_T *s = _.session; s; s = s->next ) {
			VOS_TIMEVAL_T tv;
			tlc_getInterval(s->sessionhandle, &tv, &rfds, &noOfDesc);
			if (timercmp( &tv, &zero, >) && timercmp( &tv, &max_tv, <)) max_tv = tv;
		}

		if (timercmp( &max_tv, &zero, <)) max_tv = zero;  /* max_tv must not be negative */
		int rv = vos_select( noOfDesc+1, &rfds, NULL, NULL, &max_tv);

		vos_getTime( &now );
		for (TAU_XSESSION_T *s = _.session; s; s = s->next ) {
			result = tlc_process(s->sessionhandle, &rfds, &rv);
			s->lastTime = now;
		}

	} while ( timercmp( &now, &deadline, <) );

	return result;
}

/* TODO below is potential rubbish !!__!! */
TRDP_ERR_T tau_xsession_cycle_old(TAU_XSESSION_T *our,  INT64 *timeout_us ) {
	TRDP_ERR_T result = TRDP_INIT_ERR;
	if (_.use <= 0 || (our && !tau_xsession_up(our))) return TRDP_INIT_ERR;

	int firstRound = 1;
	int leave = 0;

	TAU_XSESSION_T *s = our ? our : _.session;
	VOS_TIMEVAL_T nextTime;
	VOS_TIMEVAL_T thisTime;
	vos_getTime( &thisTime );

	do {
		nextTime.tv_sec = 0;
		nextTime.tv_usec = s->processConfig.cycleTime;
		vos_addTime( &nextTime, &s->lastTime); /* last-deadline + cycle-period */
		if (vos_cmpTime(&nextTime, &thisTime) < 0) nextTime = thisTime; /* don't have a future marker in the past */
		s->lastTime = nextTime; /* store the next deadline */
		s = s->next;
	} while (!our && s);

	do {
		INT32 noOfDesc = 0;
		fd_set rfds;
		FD_ZERO(&rfds);

		VOS_TIMEVAL_T tv, max_tv = nextTime;
		vos_subTime( &max_tv, &thisTime); /* max_tv now contains the remaining max sleep time */
		if ((max_tv.tv_sec < 0) || (!max_tv.tv_sec && (max_tv.tv_usec <= 0))) {
			max_tv.tv_sec  = 0;
			max_tv.tv_usec = 0;
		}
		if (timeout_us) { /* push timeout to external handler */
			tlc_getInterval(our->sessionhandle, (TRDP_TIME_T *) &tv, (TRDP_FDS_T *) &rfds, &noOfDesc);
			if (vos_cmpTime( &max_tv, &tv) < 0)
				*timeout_us = max_tv.tv_sec*1000000+max_tv.tv_usec;
			else
				*timeout_us =     tv.tv_sec*1000000+    tv.tv_usec;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			leave = 1;
		} else {
			if ((max_tv.tv_sec < 0) || (!max_tv.tv_sec && (max_tv.tv_usec <= 0))) {
				if (!firstRound) break;

				tlc_getInterval(our->sessionhandle, (TRDP_TIME_T *) &tv, (TRDP_FDS_T *) &rfds, &noOfDesc);
				tv.tv_sec = 0;
				tv.tv_usec = 0;
				/* do not leave before the initial round, ie, no break */
				leave = 1;

			} else {
				tlc_getInterval(our->sessionhandle, (TRDP_TIME_T *) &tv, (TRDP_FDS_T *) &rfds, &noOfDesc);
				if (vos_cmpTime( &tv, &max_tv) > 0) /* 1 on tv > max_tv */
					tv = max_tv;
			}
		}

		int rv = vos_select((int)noOfDesc+1, &rfds, NULL, NULL, &tv);

		//if (rv) vos_printLog(VOS_LOG_INFO, "Pending events: %d/%d/%2x\n", rv, noOfDesc, *((uint8_t *)&rfds));
		s = our ? our : _.session;
		do {
			result = tlc_process(s->sessionhandle, (TRDP_FDS_T *) &rfds, &rv);
			s = s->next;
		} while (!our && s);

		firstRound = 0;
		if (!leave) vos_getTime( &thisTime );
	} while (!leave);

	return result;
}

TRDP_ERR_T tau_xsession_setCom(TAU_XSESSION_T *our, UINT32 pubTelID, const UINT8 *data, UINT32 cap) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	TRDP_ERR_T result;

	if (pubTelID < MAX_TELEGRAMS) {
		result = tlp_put( our->sessionhandle, our->aTelegrams[pubTelID].handle, data, cap);
		if (result != our->aTelegrams[pubTelID].result) {
			our->aTelegrams[pubTelID].result = result;
			vos_printLog(VOS_LOG_WARNING, "\nFailed to SET comId=%d from %8x. %s\n",
					our->aTelegrams[pubTelID].comID, our->aTelegrams[pubTelID].peerID, tau_getResultString(result));
		}
	} else {
		vos_printLog(VOS_LOG_ERROR, "Invalid parameters to setCom buffer.");
		result = TRDP_PARAM_ERR;
	}

	return result;
}

TRDP_ERR_T tau_xsession_getCom(TAU_XSESSION_T *our, UINT32 subTelID, UINT8 *data, UINT32 cap, UINT32 *length, TRDP_PD_INFO_T *info) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	TRDP_ERR_T result;

	if ((subTelID < MAX_TELEGRAMS) /*&& (*length == aTelegrams[subTelID].size) */) {
		if (length) *length = cap;
		result = tlp_get( our->sessionhandle, our->aTelegrams[subTelID].handle, info, data, length);
		if (result != our->aTelegrams[subTelID].result) {
			our->aTelegrams[subTelID].result = result;
			vos_printLog(VOS_LOG_WARNING, "\nFailed to get comId=%d from src=%d (%s)\n",
					our->aTelegrams[subTelID].comID, our->aTelegrams[subTelID].peerID, tau_getResultString(result));
		}

	} else {
		vos_printLog(VOS_LOG_ERROR, "Invalid parameters to getCom buffer.");
		result = TRDP_PARAM_ERR;
	}
	if (result != TRDP_NO_ERR && length) *length = 0;

	return result;
}

TRDP_ERR_T tau_xsession_request(TAU_XSESSION_T *our, UINT32 subTelID) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	TRDP_ERR_T result;
	TRDP_SUB_T sub = our->aTelegrams[subTelID].handle;
	result = tlp_request(our->sessionhandle, sub, sub->addr.comId, 0u, 0u, our->pIfConfig->hostIp, sub->addr.srcIpAddr,
						0u, TRDP_FLAGS_NONE, 0u, NULL, 0u, 0u, 0u);
	if (result != TRDP_NO_ERR)
		vos_printLog(VOS_LOG_WARNING, "Failed to request telegram comId=%d from dst=%d (%s)",
				sub->addr.comId, sub->addr.srcIpAddr, tau_getResultString(result));

	return result;
}

TRDP_ERR_T tau_xsession_delete(TAU_XSESSION_T *our) {
	TRDP_ERR_T result = TRDP_NO_ERR;
	TAU_XSESSION_T *s = _.session;

	if (s && our) {
		if (s == our) _.session = our->next;
		else {
			while (s && s->next != our) s = s->next;
			if (s) s->next = our->next;
			s = our;
		}
		our->next = NULL;
	}

	while (tau_xsession_up(s)) {

		/*  Unpublish/unsubscribe all telegrams */
		for (UINT32 i = 0; i < s->numTelegrams; i++) {
			/* tlp_unpublish recognizes whether the handle was published */
			if (tlp_unpublish(s->sessionhandle, s->aTelegrams[i].handle) != TRDP_NO_ERR)
				tlp_unsubscribe(s->sessionhandle, s->aTelegrams[i].handle);
		}

		/* Close session */
		tlc_closeSession(s->sessionhandle);

		/*  Free allocated memory - parsed telegram configuration */
		tau_freeTelegrams(s->numExchgPar, s->pExchgPar);
		s->numExchgPar = 0;
		s->pExchgPar = NULL;
		_.use--;
		TAU_XSESSION_T *next = s->next;
		vos_memFree(s);
		s = next;
	}

	if (!_.use) {
		tau_freeXmlDatasetConfig(_.numComId, _.pComIdDsIdMap, _.numDataset, _.apDataset);
		_.session = NULL;
		_.numComId = 0;
		_.pComIdDsIdMap = NULL;
		_.numDataset = 0;
		_.apDataset = NULL;
		tau_freeXmlDoc(&_.devDocHnd);
		tlc_terminate();
		_.use--;
	}
	return result;
}

TRDP_ERR_T tau_xsession_ComId2DatasetId(TAU_XSESSION_T *our, UINT32 ComID, UINT32 *datasetId) {
	if (!tau_xsession_up(our)) return TRDP_INIT_ERR;
	if (!datasetId) return TRDP_PARAM_ERR;
	TRDP_ERR_T result = TRDP_COMID_ERR;
	for (UINT32 tlgIdx = 0; tlgIdx < our->numExchgPar; tlgIdx++) {
		if ((our->pExchgPar[tlgIdx].srcCnt || our->pExchgPar[tlgIdx].destCnt)
				&& our->pExchgPar[tlgIdx].comId == ComID) {

			*datasetId = our->pExchgPar[tlgIdx].datasetId;
			/* take only first matching */
			return TRDP_COMID_ERR;
		}
	}
	return result;
}

TRDP_ERR_T tau_xsession_lookup_dataset(UINT32 datasetId, TRDP_DATASET_T **ds) {
	if (_.use < 0) return TRDP_INIT_ERR;
	if (!ds || !datasetId) return TRDP_PARAM_ERR;
	return findDataset(datasetId, ds);
}

TRDP_ERR_T tau_xsession_lookup_variable(UINT32 datasetId, const CHAR8 *name, UINT32 index, TRDP_DATASET_ELEMENT_T **el) {
	if ( !name ^ !index ) {
		TRDP_DATASET_T *ds;
		TRDP_ERR_T err = tau_xsession_lookup_dataset(datasetId, &ds);
		if (err) return err;

		if (index <= ds->numElement) {
			index--; /* adjust from element number to C-array-index */
			for (UINT32 i=0; i<ds->numElement; i++ ) {
				if (i==index || (name && !strncasecmp(name, ds->pElement[i].name,30))) {
					*el = &ds->pElement[i];
					return TRDP_NO_ERR;
				}
			}
		}
	}
	return TRDP_PARAM_ERR;
}


