<?php

require_once('CRM/Contribute/Form/Task.php');

/**
 * This class provides the common functionality for issuing CDN Tax Receipts for
 * one or a group of contact ids.
 */
class CRM_Cdntaxreceipts_Task_IssueSingleTaxReceipts extends CRM_Contribute_Form_Task {

  const MAX_RECEIPT_COUNT = 1000;

  private $_receipts;

  /**
   * build all the data structures needed to build the form
   *
   * @return void
   * @access public
   */
  function preProcess() {

    //check for permission to edit contributions
    if ( ! CRM_Core_Permission::check('issue cdn tax receipts') ) {
      CRM_Core_Error::fatal(ts('You do not have permission to access this page', array('domain' => 'org.civicrm.cdntaxreceipts')));
    }

    parent::preProcess();

    $receipts = array( 'original'  => array('email' => 0, 'print' => 0),
                       'duplicate' => array('email' => 0, 'print' => 0), );

    // count and categorize contributions
    foreach ( $this->_contributionIds as $id ) {
      if ( cdntaxreceipts_eligibleForReceipt($id) ) {
        list($issued_on, $receipt_id) = cdntaxreceipts_issued_on($id);
        $key = empty($issued_on) ? 'original' : 'duplicate';
        list( $method, $email ) = cdntaxreceipts_sendMethodForContribution($id);
        $receipts[$key][$method]++;
      }
    }

    $this->_receipts = $receipts;

  }

  /**
   * Build the form
   *
   * @access public
   *
   * @return void
   */
  function buildQuickForm() {

    CRM_Utils_System::setTitle(ts('Issue Tax Receipts', array('domain' => 'org.civicrm.cdntaxreceipts')));

    // assign the counts
    $receipts = $this->_receipts;
    $originalTotal = $receipts['original']['print'] + $receipts['original']['email'];
    $duplicateTotal = $receipts['duplicate']['print'] + $receipts['duplicate']['email'];
    $receiptTotal = $originalTotal + $duplicateTotal;
    $this->assign('receiptCount', $receipts);
    $this->assign('originalTotal', $originalTotal);
    $this->assign('duplicateTotal', $duplicateTotal);
    $this->assign('receiptTotal', $receiptTotal);

    // add radio buttons
    $this->addElement('radio', 'receipt_option', NULL, ts('Issue tax receipts for the %1 unreceipted contributions only.', array(1=>$originalTotal, 'domain' => 'org.civicrm.cdntaxreceipts')), 'original_only');
    $this->addElement('radio', 'receipt_option', NULL, ts('Issue tax receipts for all %1 contributions. Previously-receipted contributions will be marked \'duplicate\'.', array(1=>$receiptTotal, 'domain' => 'org.civicrm.cdntaxreceipts')), 'include_duplicates');
    $this->addRule('receipt_option', ts('Selection required', array('domain' => 'org.civicrm.cdntaxreceipts')), 'required');

    $this->add('checkbox', 'is_preview', ts('Run in preview mode?', array('domain' => 'org.civicrm.cdntaxreceipts')));

    $buttons = array(
      array(
        'type' => 'cancel',
        'name' => ts('Back', array('domain' => 'org.civicrm.cdntaxreceipts')),
      ),
      array(
        'type' => 'next',
        'name' => 'Issue Tax Receipts',
        'isDefault' => TRUE,
        'js' => array('onclick' => "return submitOnce(this,'{$this->_name}','" . ts('Processing', array('domain' => 'org.civicrm.cdntaxreceipts')) . "');"),
      ),
    );
    $this->addButtons($buttons);

  }

  function setDefaultValues() {
    return array('receipt_option' => 'original_only');
  }

  /**
   * process the form after the input has been submitted and validated
   *
   * @access public
   *
   * @return None
   */

  function postProcess() {

    // lets get around the time limit issue if possible
    if ( ! ini_get( 'safe_mode' ) ) {
      set_time_limit( 0 );
    }

    // Issue 1895204: Turn off geocoding to avoid hitting Google API limits
    if (version_compare(CRM_Utils_System::version(), '4.7', '<')) {
      $config =& CRM_Core_Config::singleton();
      $oldGeocode = $config->geocodeMethod;
      unset($config->geocodeMethod);
    }
    else {
      // TODO: test this works in 4.7. Does it affect the current request?
      // better yet, figure out why Civi is triggering a geocode here, and stop it.
      $oldGeocode = cdntaxreceipts_getCiviSetting('geoProvider');
      cdntaxreceipts_setCiviSetting('geoProvider', NULL);
    }

    $params = $this->controller->exportValues($this->_name);

    $originalOnly = FALSE;
    if ($params['receipt_option'] == 'original_only') {
      $originalOnly = TRUE;
    }

    $previewMode = FALSE;
    if (isset($params['is_preview']) && $params['is_preview'] == 1 ) {
      $previewMode = TRUE;
    }

    /**
     * Drupal module include
     */
    //module_load_include('.inc','civicrm_cdntaxreceipts','civicrm_cdntaxreceipts');
    //module_load_include('.module','civicrm_cdntaxreceipts','civicrm_cdntaxreceipts');

    // start a PDF to collect receipts that cannot be emailed
    $receiptsForPrinting = cdntaxreceipts_openCollectedPDF();

    $emailCount = 0;
    $printCount = 0;
    $failCount = 0;
    foreach ($this->_contributionIds as $item => $contributionId) {

      if ( $emailCount + $printCount + $failCount >= self::MAX_RECEIPT_COUNT ) {
        $status = ts('Maximum of %1 tax receipt(s) were sent. Please repeat to continue processing.', array(1=>self::MAX_RECEIPT_COUNT, 'domain' => 'org.civicrm.cdntaxreceipts'));
        CRM_Core_Session::setStatus($status, '', 'info');
        break;
      }

      // 1. Load Contribution information
      $contribution = new CRM_Contribute_DAO_Contribution();
      $contribution->id = $contributionId;
      if ( ! $contribution->find( TRUE ) ) {
        CRM_Core_Error::fatal( "CDNTaxReceipts: Could not find corresponding contribution id." );
      }

      // 2. If Contribution is eligible for receipting, issue the tax receipt.  Otherwise ignore.
      if ( cdntaxreceipts_eligibleForReceipt($contribution->id) ) {

        list($issued_on, $receipt_id) = cdntaxreceipts_issued_on($contribution->id);
        if ( empty($issued_on) || ! $originalOnly ) {

          list( $ret, $method ) = cdntaxreceipts_issueTaxReceipt( $contribution, $receiptsForPrinting, $previewMode );

          if ( $ret == 0 ) {
            $failCount++;
          }
          elseif ( $method == 'email' ) {
            $emailCount++;
          }
          else {
            $printCount++;
          }

        }
      }
    }

    // 3. Set session status
    if ( $previewMode ) {
      $status = ts('%1 tax receipt(s) have been previewed.  No receipts have been issued.', array(1=>$printCount, 'domain' => 'org.civicrm.cdntaxreceipts'));
      CRM_Core_Session::setStatus($status, '', 'success');
    }
    else {
      $status = ts('%1 tax receipt(s) were sent by email.', array(1=>$emailCount, 'domain' => 'org.civicrm.cdntaxreceipts'));
      CRM_Core_Session::setStatus($status, '', 'success');
      $status = ts('%1 tax receipt(s) need to be printed.', array(1=>$printCount, 'domain' => 'org.civicrm.cdntaxreceipts'));
      CRM_Core_Session::setStatus($status, '', 'success');
    }

    if ( $failCount > 0 ) {
      $status = ts('%1 tax receipt(s) failed to process.', array(1=>$failCount, 'domain' => 'org.civicrm.cdntaxreceipts'));
      CRM_Core_Session::setStatus($status, '', 'error');
    }

    // Issue 1895204: Reset geocoding
    if (version_compare(CRM_Utils_System::version(), '4.7', '<')) {
      $config =& CRM_Core_Config::singleton();
      $config->geocodeMethod = $oldGeocode;
    }
    else {
      cdntaxreceipts_setCiviSetting('geoProvider', $oldGeocode);
    }


    // 4. send the collected PDF for download
    // NB: This exits if a file is sent.
    cdntaxreceipts_sendCollectedPDF($receiptsForPrinting, 'Receipts-To-Print-' . (int) $_SERVER['REQUEST_TIME'] . '.pdf');  // EXITS.
  }
}

