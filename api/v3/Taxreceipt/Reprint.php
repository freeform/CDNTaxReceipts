<?php

/**
 * Taxreceipt.Reprint API specification (optional)
 * This is used for documentation and validation.
 *
 * @param array $spec description of fields supported by this API call
 * @return void
 * @see http://wiki.civicrm.org/confluence/display/CRM/API+Architecture+Standards
 */
function _civicrm_api3_Taxreceipt_Reprint_spec(&$spec) {
  //$spec['contact_id']['api.required'] = 1;
  $spec['receipt']['api.required'] = 1;
  $spec['collected_pdf_obj']['api.required'] = 1;
  $spec['preview_mode']['api.default'] = 0;
}

/**
 * Taxreceipt.Reprint API
 *
 * @param array $params
 * @return array API result descriptor
 * @see civicrm_api3_create_success
 * @see civicrm_api3_create_error
 * @throws API_Exception
 */
function civicrm_api3_Taxreceipt_Reprint($params) {
  //TODO: Validate all params
  if (array_key_exists('collected_pdf_obj', $params)) {

    //TODO: Make sure this works for issuing
    $receipt = $params['receipt']; //_civicrm_api3_taxreceipt_build($params);
    $previewMode = array_key_exists('preview_mode', $params) ? $params['preview_mode'] : 0;
    $collectedPdf = $params['collected_pdf_obj'];

    list($returnValue, $user_friendly) = cdntaxreceipts_reprintReceipt($receipt, $collectedPdf, $previewMode);
    return civicrm_api3_create_success($returnValue, $params, 'taxreceipt', 'reprint');
  } else {
    throw new API_Exception(/*errorMessage*/ 'Could not issue receipt', /*errorCode*/ 1);
  }
}

