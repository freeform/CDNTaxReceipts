<?php

/**
 * Taxreceipt.Log API specification (optional)
 * This is used for documentation and validation.
 *
 * @param array $spec description of fields supported by this API call
 * @return void
 * @see http://wiki.civicrm.org/confluence/display/CRM/API+Architecture+Standards
 */
function _civicrm_api3_taxreceipt_Log_spec(&$spec) {
  $spec['contact_id']['api.required'] = 1;
  $spec['receipt']['api.required'] = 1;
/*  $spec['contributions'] = array(
    'name' => 'contributions',
    'title' => 'Contributions',
    'description' => 'Contribution(s) receipted on this receipt.',
    'type' => 1,
    'FKClassName' => 'CRM_Contribute_DAO_Contribution',
    'FKApiName' => 'Contribution',
  );*/
  /* $spec['issued']['api.required'] = 1; */
}

/**
 * Taxreceipt.Log API
 *
 * @param array $params
 * @return array API result descriptor
 * @see civicrm_api3_create_success
 * @see civicrm_api3_create_error
 * @throws API_Exception
 */
function civicrm_api3_taxreceipt_Log($params) {
  if (array_key_exists('contact_id', $params)) {
    $result = FALSE;
    $receipt = $params['receipt']; // _civicrm_api3_taxreceipt_build($params);
    //if ($receipt['is_error'] == 0) {
      $result = taxreceipts_log($receipt);
    //}
    if($result) {
      // Spec: civicrm_api3_create_success($values = 1, $params = array(), $entity = NULL, $action = NULL)
      return civicrm_api3_create_success($result, $params, 'TaxReceipt', 'log');
    }
    else {
      throw new API_Exception('Unable to log tax receipt.', /*errorCode*/ 1);
    }
  } else {
    throw new API_Exception('Missing parameters contact_id.', /*errorCode*/ 2);
  }
}

function  _civicrm_api3_taxreceipt_build($params) {
  $receipt = array(
    'is_error' => 0,
  );

  $receipt_defaults = array(
    'is_duplicate' => 0,
  );
  $receipt_params = array(
    'receipt_no',
    'issued_on',
    'contact_id',
    'receipt_amount',
    'is_duplicate',
    'issue_type',
    'issue_method',
  );

  foreach ($receipt_params as $rp) {
    if(!isset($receipt_defaults[$rp]) && !isset($params[$rp])) {
      $receipt['is_error'] = 1;
    }
    else if(isset($receipt_defaults[$rp]) && !isset($params[$rp])) {
      $receipt[$rp] = $receipt_defaults[$rp];
    }
    else if(isset($params[$rp])){
      $receipt[$rp] = $params[$rp];
    }
  }

  $contrib_params = array(
    'contribution_id',
    'contribution_amount',
    'receipt_amount',
    'receive_date',
  );

  foreach ($params['contributions'] as $contribution ) {
    $contrib_error = FALSE;
    foreach ($contrib_params as $cp) {
      if(!isset($contribution[$cp])) {
        $receipt['is_error'] = 1;
        $contrib_error = TRUE;
      }
    }
    if (!$contrib_error) {
      $receipt['contributions'][] = $contribution;
    }
  }

  return $receipt;
}
