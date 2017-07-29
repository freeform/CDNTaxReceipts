<?php

/**
 * Taxreceipt.Issue API specification (optional)
 * This is used for documentation and validation.
 *
 * @param array $spec description of fields supported by this API call
 * @return void
 * @see http://wiki.civicrm.org/confluence/display/CRM/API+Architecture+Standards
 *
 * 'collected_pdf_file_op' => One of 'NONE', 'NEW', 'CLOSE'
 */
function _civicrm_api3_taxreceipt_Issue_spec(&$spec) {
  //$spec['contact_id']['api.required'] = 1;
  $spec['receipt']['api.required'] = 1;
  $spec['collected_pdf_obj']['api.required'] = 1;
  $spec['preview_mode']['api.default'] = 0;
}

/**
 * Taxreceipt.Issue API
 *
 * @param array $params
 * @return array API result descriptor
 * @see civicrm_api3_create_success
 * @see civicrm_api3_create_error
 * @throws API_Exception
 */
function civicrm_api3_taxreceipt_Issue($params) {
  //TODO: Validate all params
  if (array_key_exists('collected_pdf_obj', $params)) {

    $receipt_defaults = array(
      'is_duplicate' => 0,
      'receipt_status' => 'issued',
    );
    $receipt = array_merge($receipt_defaults, $params['receipt']);
    $previewMode = array_key_exists('preview_mode', $params) ? $params['preview_mode'] : 0;
    $collectedPdf = $params['collected_pdf_obj'];

    list($returnValue, $method, $singlePdfOb) = cdntaxreceipts_processTaxReceipt($receipt, $collectedPdf, $previewMode);
    return civicrm_api3_create_success($returnValue, $params, 'taxreceipt', 'issue');
  } else {
    throw new API_Exception(/*errorMessage*/ 'Could not issue receipt', /*errorCode*/ 1);
  }
}

/**
 * Taxreceipt.Issue API specification (optional)
 * This is used for documentation and validation.
 *
 * @param array $spec description of fields supported by this API call
 * @return void
 * @see http://wiki.civicrm.org/confluence/display/CRM/API+Architecture+Standards
 *
 */
function _civicrm_api3_taxreceipt_IssueBatch_spec(&$spec) {
  $spec['issue_type']['api.required'] = 1;
  $spec['issue_method']['api.required'] = 1;
  $spec['receipts']['api.required'] = 1;
  $spec['collected_pdf_file_path']['api.required'] = 1;
  $spec['preview_mode']['api.default'] = 0;
}

/**
 * Taxreceipt.Issue API
 *
 * @param array $params
 * @return array API result descriptor
 * @see civicrm_api3_create_success
 * @see civicrm_api3_create_error
 * @throws API_Exception
 */
function civicrm_api3_taxreceipt_IssueBatch($params) {
  //TODO: Validate all params
  if (array_key_exists('collected_pdf_obj', $params)) {

    $issue_params = array();
    //TODO: Open the PDF

    //TODO: Foreach receipt add to the batch using
    $result = civicrm_api3('taxreceipt', 'issue', $issue_params);

    //TODO: Close and write the result PDF file

    return civicrm_api3_create_success($returnValue, $params, 'taxreceipt', 'issue');
  } else {
    throw new API_Exception(/*errorMessage*/ 'Could not issue receipt', /*errorCode*/ 1);
  }
}


