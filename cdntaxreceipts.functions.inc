<?php

require_once 'tcpdf/tcpdf.php';
require_once('FPDI/fpdi.php');

  class PDF extends FPDI {
    /**
     * "Remembers" the template id of the imported page
     */
    var $_tplIdx;

    /**
     * include a background template for every page
     */
    function Header() {
      $pdf_template_file = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_pdftemplate');
      if (!empty($pdf_template_file)) {

        if (is_null($this->_tplIdx)) {
          $pdf_template_file = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_pdftemplate');
          $this->setSourceFile($pdf_template_file);
          $this->_tplIdx = $this->importPage(1);
        }
        $this->useTemplate($this->_tplIdx);
      }
      else {
        $this->parsers = array();
      }
    }

    function Footer() {}
  }



  const CDNTAX_SETTINGS = 'CDNTaxReceipts';

/**
 * processTaxReceipt()
 * Accepts an associative array containing receipt variables, and:
 * - generates a PDF file using the provided variables
 * - sends a copy of the receipt to the email archive
 * - emails the receipt to the donor if issue_method='email'
 * - logs the receipt to the audit log
 *
 * This method is common to both single and annual receipts.
 */
function cdntaxreceipts_processTaxReceipt($receipt, &$collectedPdf = NULL, $previewMode = FALSE, $method = NULL) {

  // Get contact details
  list($displayname, $email) = CRM_Contact_BAO_Contact::getContactDetails($receipt['contact_id']);

  $serial_receipt = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_serial');
  if ($serial_receipt && empty($receipt['is_duplicate'])) {
    // use my experimental alternative to generating serial receipt numbers, unless this is a re-issue
    $lock = new CRM_Core_Lock('cdntaxreceipts.processTaxReceipt');
    if (!$lock->acquire()) {
      $userAlert = ts('Failed to acquire lock. No receipts were processed.');
      CRM_Core_Session::setStatus($userAlert, ts('Warning'), 'alert');
      return array(FALSE,'',NULL);
    }
    $next_id = cdntaxreceipts_log_next_id();
    $receipt['receipt_no'] = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_prefix') . str_pad($next_id, 8, 0, STR_PAD_LEFT);
  }
  // allow modules to alter any details
  // invoke hook_cdntaxreceipts_alter_receipt(&$receipt)
  //   modules should accept a reference to the $receipt array and alter it directly if they wish
  //   one useful use case: alter the $receipt['source'] line to include custom text
  //   to alter receipt amount, use hook_cdntaxreceipts_eligibleAmount() instead
  if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    CRM_Utils_Hook::singleton()->invoke(1, $receipt, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_alter_receipt');
  } else {
    CRM_Utils_Hook::singleton()->invoke(1, $receipt, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_alter_receipt');
  }

  // generate the PDF file
  list($pdf_file, $user_friendly) = cdntaxreceipts_generateFormattedReceipt($receipt, $collectedPdf, $previewMode);

  // form a mailParams array to pass to the CiviCRM mail utility
  $attachment = array(
    'fullPath' => $pdf_file,
    'mime_type' => 'application/pdf',
    'cleanName' => $user_friendly,
  );

  $email_message = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'email_message');
  $org_name = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_name');

  $email_subject_admin = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'email_subject');
  $email_subject = $email_subject_admin . " " . $receipt['receipt_no'];

  $email_from = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'email_from');
  $email_archive = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'email_archive');

  $mailparams = array(
    'from' => $org_name . ' <' . $email_from . '> ',
    'toName' => $displayname,
    'attachments' => array( $attachment ),
    'subject' => $email_subject,
    'text' => $email_message,
    'returnPath' => $email_from,
  );

  // if method is email, we send to contact and BCC email archive.
  // if method is print, we send to email archive only.
  if ( $receipt['issue_method'] == 'email' ) {
    $mailparams['toEmail'] = $email;
    $mailparams['bcc'] = $email_archive;
  }
  else {
    $mailparams['toEmail'] = $email_archive;
  }

  if ( $previewMode ) {
    $ret = TRUE;
  }
  else {
    $ret = CRM_Utils_Mail::send( $mailparams );
    if ( $ret == TRUE ) {
      // we have successfully processed.  Log the receipt.
      cdntaxreceipts_log($receipt);
    }
  }
  if (!empty($lock)) {
    $lock->release();
  }

  if ( $receipt['issue_method'] == 'email' ) {
    unlink($pdf_file); // file is no longer needed
    return array( $ret, 'email', NULL );
  }
  else {
    if ( isset($collectedPdf) ) {
      unlink($pdf_file); // file is no longer needed
      $pdf_file = NULL;
    }
    // if not unset, cron will clean up the PDF file later on
    return array( $ret, 'print', $pdf_file );
  }
}

/**
 * cdntaxreceipts_reprintReceipt()
 * Accepts an associative array containing receipt variables, and:
 * - generates a PDF file using the provided variables
 *
 * This method is common to both single and annual receipts.
 */
function cdntaxreceipts_reprintReceipt( $receipt, &$collectedPdf = NULL, $previewMode = FALSE) {

  // allow modules to alter any details
  // invoke hook_cdntaxreceipts_alter_receipt(&$receipt)
  //   modules should accept a reference to the $receipt array and alter it directly if they wish
  //   one useful use case: alter the $receipt['source'] line to include custom text
  //   to alter receipt amount, use hook_cdntaxreceipts_eligibleAmount() instead
  if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    CRM_Utils_Hook::singleton()->invoke(1, $receipt, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_alter_receipt');
  } else {
    CRM_Utils_Hook::singleton()->invoke(1, $receipt, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_alter_receipt');
  }

  // Generate the PDF file
  list($pdf_file, $user_friendly) = cdntaxreceipts_generateFormattedReceipt($receipt, $collectedPdf, $previewMode);
  // if not unset, cron will clean up the PDF file later on
  return array(TRUE, 'print', $pdf_file );
}

/**
 * generateFormattedReceipt()
 * Accepts an associative array of receipt variables and produces a PDF file.
 * This function should always be called through processTaxReceipt().  It is imperative
 * that each receipt generated by the system gets logged and archived properly.
 */
function cdntaxreceipts_generateFormattedReceipt($receipt, &$collectedPdf = NULL, $previewMode = FALSE) {
  //  define ('K_PATH_IMAGES', '');
  require_once 'tcpdf/tcpdf.php';

  $pdf_img_files_path = dirname(__FILE__) . '/img/';

  $address = cdntaxreceipts_getAddress($receipt['contact_id']);

  $address_line_1 = isset($address['street_address']) ? $address['street_address'] : '';
  $parts = array();
  foreach(array('city', 'state_province', 'postal_code')  as $addr_part) {
    if (isset($address[$addr_part])) {
      $parts[] = $address[$addr_part];
    }
  }
  $address_line_1b = isset($address['supplemental_address_1']) ? $address['supplemental_address_1'] : '';
  $address_line_1c = isset($address['supplemental_address_2']) ? $address['supplemental_address_2'] : '';
  $address_line_2 = implode(' ', $parts);
  $address_line_3 = isset($address['country']) ? $address['country'] : '';

  list($displayname, $email) = CRM_Contact_BAO_Contact::getContactDetails($receipt['contact_id']);
  if ( $receipt['issue_type'] == 'single' ) {
    $pos = strpos($receipt['receive_date'], '-');
    if ($pos === FALSE) {
      $date = substr($receipt['receive_date'], 0, 8);
      $display_date = substr($date, 0, 4) . '-' . substr($date, 4, 2) . '-' . substr($date, 6, 2);
    }
    else {
      $display_date = substr($receipt['receive_date'], 0, 10);
    }
    $display_year = '';
  }
  else {
    $max_date = '1900-01-01';
    foreach($receipt['contributions'] as $contrib) {
      if (strcmp($contrib['receive_date'], $max_date) > 0) {
        $max_date = $contrib['receive_date'];
      }
      $display_date = substr($max_date, 0, 10);
    }
  }

  $line_1 = "This is your Official Receipt for income tax purposes.";

  $config = CRM_Core_Config::singleton();
  $pdf_file = $config->customFileUploadDir . 'Receipt-' . $receipt['receipt_no'] . '.pdf';
  $user_friendly = 'Receipt-' . $receipt['receipt_no'] . '.pdf';

  if (!defined('pdf_page_format')) {
    define('PDF_PAGE_FORMAT', 'USLETTER', true); // orig: 'A4'
  }
  $pdf = new PDF(PDF_PAGE_ORIENTATION, PDF_UNIT, 'LETTER', TRUE, 'UTF-8', FALSE);
  $pdf->Open();

  $pdf->SetAuthor(CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_name'));

  $mymargin_left = 12;
  $mymargin_top = 6;
  $mymargin_right = 12;
  $pdf->SetMargins($mymargin_left, $mymargin_top, $mymargin_right);

  $pdf->setJPEGQuality('100');

  $pdf->SetAutoPageBreak('', $margin=0);

  //TODO: Move the chunk below into a hook
  // Freeform: Get extra variables
  // Receieved by: (target Entity), Contribution Type (GS or MO)
  // Also alter receive date to EO date
  $received_by = 'Green Party of Ontario';
  $contribution_type = '';

  foreach ($receipt['contributions'] as $key => $this_contribution) {
    $contribution_id =  $this_contribution['contribution_id'];
    $params = array(
      'id' => $contribution_id,
      'sequential' => FALSE,
      'return.custom_46' => 1,
      'return.custom_47' => 1,
      'return.custom_50' => 1,
    );
    $results = civicrm_api3('Contribution', 'get', $params);

    if ($results['is_error'] == 0 && $results['count'] == 1) {
      $contrib = $results['values'][$contribution_id];

      if(isset($contrib['custom_46'])) {
        $received_by_id = $contrib['custom_46'];
        if (!empty($received_by_id)) {
          $target_params = array(
            'id' => $received_by_id,
            'sequential' => FALSE,
          );
          $target_results = civicrm_api3('Contact', 'get', $target_params);
          if ($target_results['is_error'] == 0 && $target_results['count'] == 1) {
            $target_contact = $target_results['values'][$received_by_id];
            if(!empty($target_contact['legal_name'])) {
              $received_by = $target_contact['legal_name'];
            }
            else if(!empty($target_contact['organization_name'])) {
              $received_by = $target_contact['organization_name'];
            }
          }
        }
      }
      if(isset($contrib['custom_47'])) {
        $contribution_type = ($contrib['custom_47'] == 0) ? 'Monetary' : 'Goods and Services';
      }
/*      if(isset($contrib['custom_50'])) {
        $receipt['contributions'][$key]['receive_date'] = $contrib['custom_50'];
      }*/
    }
  }

  // Contributor Type
  $contributor_type = '';
  $contact = array();
  $params = array(
    'id' => $receipt['contact_id'],
    'sequential' => FALSE,
  );
  $results = civicrm_api3('Contact', 'get', $params);

  if ($results['is_error'] == 0 && $results['count'] == 1) {
    $contact = $results['values'][$receipt['contact_id']];
  }

  if ($contact['contact_type'] == 'Individual') {
    $contributor_type = 'Individual';
  }
  else if ($contact['contact_type'] == 'Organization') {
    if (strpos($contact['contact_sub_type'][0], 'Union') !=  FALSE) {
      $contributor_type = 'Trade Union';
    }
    else if (empty($contact['contact_sub_type'][0])) {
      $contributor_type = '';
    }
    else {
      $contributor_type = 'Corporate';
    }
  }


  $pdf_variables = array(
    "preview_mode" => $previewMode,
    "mymargin_left" => $mymargin_left,
    "mymargin_top" => $mymargin_top,
    "is_duplicate" => $receipt['is_duplicate'],
    "pdf_img_files_path" => $pdf_img_files_path,
    "line_1" => $line_1,
    "source_funds" => isset($receipt['source']) ? $receipt['source'] : '',
    "amount" => $receipt['receipt_amount'],
    "display_date" => $display_date,
    "display_year" => $display_year,
    "issued_on" => date('Y-m-d', $receipt['issued_on']),
    "issue_type" => $receipt['issue_type'],
    "receipt_number" => $receipt['receipt_no'],
    "displayname" => $displayname,
    "address_line_1" => $address_line_1,
    "address_line_1b" => $address_line_1b,
    "address_line_1c" => $address_line_1c,
    "address_line_2" => $address_line_2,
    "address_line_3" => $address_line_3,
    "inkind_values" => isset($receipt['inkind_values']) ? $receipt['inkind_values'] : array(),
    "receipt_contributions" => $receipt['contributions'],
    "receipt_status" => $receipt['receipt_status'],
    'received_by' => $received_by,
    'contributor_type' => $contributor_type,
    'contribution_type' => $contribution_type,
  );

  $output_files = array($pdf);
  if ( isset($collectedPdf) && ( $receipt['issue_method'] == 'print' || $previewMode ) ) {
    $output_files[] = $collectedPdf;
  }

  foreach ( $output_files as $f ) {
    $f->AddPage();

    if ($receipt['is_duplicate']) {
      // print duplicate copy
      cdntaxreceipts_createPDF($f, $pdf_variables);
    }
    elseif (! $receipt['is_duplicate']) {
      // print original AND duplicate copy
      $pdf_variables["mymargin_top"] = $mymargin_top;
      $pdf_variables["is_duplicate"] = FALSE;
      cdntaxreceipts_createPDF($f, $pdf_variables);
      $pdf_variables["mymargin_top"] = $mymargin_top + 140;
      $pdf_variables["is_duplicate"] = TRUE;
      cdntaxreceipts_createPDF($f, $pdf_variables);
    }
  }

  // close and output the single receipt, but not the collected receipts...
  $pdf->Close();
  $pdf->Output($pdf_file, 'F');

  return array($pdf_file, $user_friendly);
}

function cdntaxreceipts_createPDF(&$pdf, $pdf_variables) {

  // Extract variables
  $preview_mode = $pdf_variables["preview_mode"];
  $mymargin_left = $pdf_variables["mymargin_left"];
  $mymargin_top = $pdf_variables["mymargin_top"];
  $is_duplicate = $pdf_variables["is_duplicate"];
  $pdf_img_files_path = $pdf_variables["pdf_img_files_path"];
  $line_1 = $pdf_variables["line_1"];
  $source_funds = $pdf_variables["source_funds"];
  $issued_on = $pdf_variables["issued_on"];
  $receipt_number = $pdf_variables["receipt_number"];
  $receipt_status = $pdf_variables['receipt_status'];

  // Extra variables added for GPO
  $received_by = $pdf_variables["received_by"]; // TODO: Get this

  $pdf_template_file = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_pdftemplate');
  if (!empty($pdf_template_file)) {
  }
  else {
    $background_image = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_watermark');
    if ( $background_image ) $pdf->Image(CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_watermark'),
      $mymargin_left + 94, $mymargin_top + 34, '', 70);
  }

  // Middle center section
  if ( $preview_mode ) {
    $pdf->Image($pdf_img_files_path . 'preview_mode.png', $mymargin_left + 42, $mymargin_top + 65, '', 42);
  }
  else if ($receipt_status == 'cancelled') {
    $pdf->Image($pdf_img_files_path . 'cancelled_trans.png', $mymargin_left + 65, $mymargin_top, '', 45);
  }
  else if ( $is_duplicate ) {
    $pdf->Image($pdf_img_files_path . 'duplicate_trans.png', $mymargin_left + 42, $mymargin_top + 65, '', 42);
  }

  // Top left section
  $pdf->Image(CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_logo', NULL, $pdf_img_files_path . 'your-logo.png'),
    $mymargin_left, $mymargin_top, '', 32);

  // Left section, received by to issued date
  $received_by_block_y_start = 35;
  $pdf->SetFont('Helvetica', 'B', 8);
  $pdf->SetXY($mymargin_left, $mymargin_top + $received_by_block_y_start);
  $pdf->Write(10, "Received By: " . $pdf_variables["received_by"]);
  $pdf->SetFont('Helvetica', 'B', 8);
  $pdf->SetY($mymargin_top + $received_by_block_y_start + 6);
  $pdf->Write(10, "Receipt No: " . $receipt_number);
  $pdf->SetFont('Helvetica', 'B', 8);
  $pdf->SetY($mymargin_top + $received_by_block_y_start + 12);
  $pdf->Write(10, "Issued Date: ". $issued_on, '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);


  // Org Address block, lower left (Was Top right section)
  $pdf->SetFont('Helvetica', 'B', 8);
  $org_block_y_start = 88;
  $block_offset = 0;
  $pdf->SetXY($mymargin_left, $mymargin_top + $org_block_y_start);
  $pdf->Write(10, CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_name'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  $pdf->SetFont('Helvetica', '', 8);
  $block_offset += 4;
  $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
  $pdf->Write(10, CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_address_line1'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  $block_offset += 4;
  $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
  $pdf->Write(10, CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_address_line2'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  $block_offset += 8;
  $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
  $pdf->Write(10, 'Tel: ' . CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_tel'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);

  if ( CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_fax' ) != '' ) {
    $block_offset += 4;
    $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
    $pdf->Write(10, 'Fax: ' . CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_fax'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  }
  $block_offset += 4;
  $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
  $pdf->Write(10, 'Email: ' . CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_email'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  $block_offset += 4;
  $pdf->SetY($mymargin_top + $org_block_y_start + $block_offset);
  $pdf->Write(10, 'Website: ' . CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_web'), '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);


  // Right section: eligible amount, received on, contribution type, contributor type, contributor address
  $x_details_column = 100;
  $x_details_column_labels = -50;
  $y_detailscolumnstart = 44;

  //_print_details_block($pdf, $pdf_variables, $mymargin_left, $x_details_column_labels, $mymargin_top,
  //  $y_detailscolumnstart, $x_details_column);
  _print_details_block2($pdf, $pdf_variables, $mymargin_left, $mymargin_top);

  // If this is an in-kind donation
  // TODO restore if needed

  // This is your receipt section top (Was Bottom left section)
  $pdf->SetFont('Helvetica', 'I', 10);
  $pdf->SetXY($mymargin_left + 80, $mymargin_top + 4);
  $pdf->Write(10, $line_1, '', 0, 'C', TRUE, 0, FALSE, FALSE, 0);
  $pdf->SetXY($mymargin_left + 80, $mymargin_top + 10);
  $pdf->Write(10, 'Thank you!', '', 0, 'C', TRUE, 0, FALSE, FALSE, 0);

  // Signature block
  $signature = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_authorized_signature_text');
  if ( $signature == '' ) {
    $signature = ts('Authorized Signature', array('domain' => 'org.civicrm.cdntaxreceipts'));
  }
  $sig_offset = strlen($signature) - 20;


  $pdf->Image(CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_signature',
    NULL, $pdf_img_files_path . 'authorized-signature.png'), $mymargin_left + 3, $mymargin_top + 62, '', 15);
  $pdf->Line($mymargin_left + 2, $mymargin_top + 76, $mymargin_left + 52, $mymargin_top + 76);
  $pdf->SetXY($mymargin_left + 8 - $sig_offset, $mymargin_top + 80);
  $pdf->SetFont("Helvetica", "I", 7);
  $pdf->Write(10, $signature);

  // Line at the bottom
  $pdf->Line($mymargin_left, $mymargin_top + 130, 198, $mymargin_top + 130, 'dash');
}

/**
 * @param $pdf
 * @param $pdf_variables
 * @param $mymargin_left
 * @param $x_details_column_labels
 * @param $mymargin_top
 * @param $y_detailscolumnstart
 * @param $x_details_column
 */
function _print_details_block(&$pdf, $pdf_variables, $mymargin_left, $x_details_column_labels, $mymargin_top, $y_detailscolumnstart, $x_details_column) {
  $block_offset = 0;
  $pdf->SetFont('Helvetica', 'B', 10);
  $pdf->SetXY($mymargin_left + $x_details_column_labels, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "Eligible Amount:", '', FALSE, 'R');
  $pdf->SetFont('Helvetica', '', 10);
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "$" . number_format($pdf_variables['amount'], 2));
  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "Received on: " . $pdf_variables['display_date']);
  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "Contribution Type: " . $pdf_variables["contribution_type"]);
  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "Contributor Type: " . $pdf_variables["contributor_type"]);

  $pdf->SetFont('Helvetica', 'B', 10);
  $block_offset += 8;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, "Received from: ", '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);

  $block_offset += 8;
  $pdf->SetFont('Helvetica', '', 10);
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, $pdf_variables['displayname'], '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);

  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, $pdf_variables['address_line_1'], '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  if ($pdf_variables['address_line_1b']) {
    $block_offset += 6;
    $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
    $pdf->Write(10, $pdf_variables['address_line_1b'], '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  }
  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, $pdf_variables['address_line_2'], '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
  $block_offset += 6;
  $pdf->SetXY($mymargin_left + $x_details_column, $mymargin_top + $y_detailscolumnstart + $block_offset);
  $pdf->Write(10, $pdf_variables['address_line_3'], '', 0, 'L', TRUE, 0, FALSE, FALSE, 0);
}

function _print_details_block2($pdf, $pdf_variables, $x_offset, $y_offset) {
  // Right column labels:
  $pdf->SetFont('helvetica', 'B', 10, '', true);
  $pdf->setCellHeightRatio(1);
  $txt = <<<EOF
Eligible Amount:\n
Received on:\n
Contribution Type:\n
Contributor Type:\n
Received from:
EOF;
  $pdf->MultiCell(40, 0, $txt,  0, 'R', false, 1, $x_offset + 92, $y_offset + 47);

  $pdf->SetFont('helvetica', '', 10, '', true);
  $amt_fmt = number_format($pdf_variables['amount'],2);
  $txt = <<<EOF
\${$amt_fmt}\n
{$pdf_variables['display_date']}\n
{$pdf_variables['contribution_type']}\n
{$pdf_variables['contributor_type']}\n
{$pdf_variables['displayname']}\n
EOF;
  $pdf->MultiCell(0, 0, $txt,  0, 'L', false, 1, $x_offset + 134, $y_offset + 47);

  //Donor Address
  $pdf->setCellHeightRatio(0.6);
  if ($pdf_variables['address_line_1b'] && $pdf_variables['address_line_1c']) {
    $txt =
"{$pdf_variables['address_line_1']}\n
{$pdf_variables['address_line_1b']}\n
{$pdf_variables['address_line_1c']}\n
{$pdf_variables['address_line_2']}\n
{$pdf_variables['address_line_3']}";
  }
  else if ($pdf_variables['address_line_1b']) {
    $txt =
"{$pdf_variables['address_line_1']}\n
{$pdf_variables['address_line_1b']}\n
{$pdf_variables['address_line_2']}\n
{$pdf_variables['address_line_3']}";
  }
  else {
    $txt =
"{$pdf_variables['address_line_1']}\n
{$pdf_variables['address_line_2']}\n
{$pdf_variables['address_line_3']}";
  }

  $pdf->MultiCell(0, 0, $txt,  0, 'L', false, 1, $x_offset + 134, $y_offset + 80);
}

/**************************************
 * SECTION: Utility Functions
 */

/**
 * set up the custom field structure for In-Kind tax receipts
 */

function cdntaxreceipts_configure_inkind_fields() {

  // check if the In-kind contribtion type exists.  if not, create it.
  $dao = _cdntaxreceipts_get_contribution_dao();
  $dao->name = 'In-kind';

  if ( ! $dao->find(true) ) {
    $dao->name = 'In-kind';
    $dao->is_deductible = TRUE;
    $dao->is_active = TRUE;
    $dao->save();
  }

  $contrib_type_id = $dao->id;

  // check if the custom group exists.  if not, create it.
  $params = array(
    'title' => 'In-kind donation fields',
    'version' => 3,
  );

  require_once 'api/api.php';
  $result = civicrm_api( 'custom_group', 'get', $params );

  if ( $result['count'] == 0 ) {
    $group = array(
      'title' => 'In-kind donation fields',
      'extends' => array( 'Contribution' ),
      'extends_entity_column_value' => array( $contrib_type_id ),
      'collapse_display' => 0,
      'style' => 'Inline',
      'is_active' => 1,
      'version' => 3
    );
    $result =& civicrm_api('custom_group', 'create', $group);
  }
  foreach ( $result['values'] as $id => $detail ) {
    $custom_group_id = $id;
  }

  // check if the custom fields exist.  if not, create them.
  foreach ( array( 'Description of property', 'Appraised by', 'Address of Appraiser', 'Original cost' ) as $field ) {
    $params = array(
      'custom_group_id' => $custom_group_id,
      'label' => $field,
      'version' => 3,
    );
    $result = civicrm_api( 'custom_field', 'get', $params);

    if ( $result['count'] == 0 ) {
      $field_params = array(
        'custom_group_id' => $custom_group_id,
        'label' => $field,
        'data_type' => 'String',
        'is_required' => 1,
        'is_active' => 1,
        'version' => 3,
      );
      if ( $field == 'Original cost' ) {
        $field_params['is_required'] = 0;
        $field_params['help_post'] = 'Amount originally paid for the item. This item is needed for a special rule'.
          ' that applies if either something was purchased in the last ten years'.
          ' with the intent of making a donation, or if an item is donated within three years'.
          ' of being purchased. In both cases the amount that can be claimed is the lesser'.
          ' of the market value, and the amount paid for the item. This item is used to show'.
          ' the cost of the item donated (and the creditable amount) if either of those '.
          ' situations apply.';
      }
      $result =& civicrm_api('custom_field', 'create', $field_params);
    }
  }
}

/**************************************
 * SECTION: Tax Receipt API
 */

/**
 * issueTaxReceipt()
 * Issues a tax receipt for a single contribution.
 * Detects if the receipt is a duplicate and reacts appropriately.
 */
function cdntaxreceipts_issueTaxReceipt( $contribution, &$collectedPdf = NULL, $previewMode = FALSE ) {

  // This function basically needs to set up a $receipt[] array and pass it to
  // processTaxReceipt() which does the hard work
  $receipt = array();

  // check if a Tax Receipt has already been issued previously for this Contribution
  // user input could be tainted! we want this method to check everything for validity...
  list($issued_on, $receipt_id) = cdntaxreceipts_issued_on($contribution->id);

  // When returning issued_on field:
  //   returns false for non existing $contribution ID
  //   returns 0 for existing but old data
  //   returns timestamp for new data
  $is_duplicate = empty($issued_on) ? 0 : 1;

  if ( $is_duplicate ) {
    // if this was issued as part of an annual receipt, then jump over to the Annual method
    $receipt = cdntaxreceipts_load_receipt($receipt_id);
    if ( $receipt['issue_type'] == 'annual' ) {
      $contactId = $receipt['contact_id'];
      $year = substr($receipt['contributions'][0]['receive_date'], 0, 4);
      return cdntaxreceipts_issueAnnualTaxReceipt($contactId, $year, $collectedPdf, $previewMode);
    }
    else if ( $receipt['issue_type'] == 'aggregate' ) {
      $contactId = $receipt['contact_id'];
      $year = substr($receipt['contributions'][0]['receive_date'], 0, 4);
      list($method, $email) = cdntaxreceipts_sendMethodForContact($contactId);
      return cdntaxreceipts_issueAggregateTaxReceipt($contactId, $year, $receipt['contributions'],
        $method, $collectedPdf, $previewMode);
    }
    // be sure I'm not changing the receipt number
    $receipt_no = $receipt['receipt_no'];
  }
  else { // generate a receipt number
    $receipt_no = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_prefix') . str_pad($contribution->id, 8, 0, STR_PAD_LEFT);
  }

  // determine the send method
  list($method, $email) = cdntaxreceipts_sendMethodForContact($contribution->contact_id);

  // Cancelled receipts are not sent via email
  if ($is_duplicate && $receipt['receipt_status'] == 'cancelled') {
    $method = 'print';
  }

  // process In-Kind variables if this is a receipt for an in-kind contribution

  $contributiontype =  _cdntaxreceipts_get_type_for_contribution($contribution);

  $inkind_values = array();
  // check if this is an 'In-kind" contribution.
  if ( $contributiontype->name == 'In-kind' ) {
    // in this case get the custom field values:
    require_once 'CRM/Core/BAO/CustomField.php';
    $groupTitle = 'In-kind donation fields';
    $fieldLabel_description = 'Description of property';
    $customFieldID_description = CRM_Core_BAO_CustomField::getCustomFieldID( $fieldLabel_description, $groupTitle );
    $fieldLabel_appraisedby = 'Appraised by';
    $customFieldID_appraisedby = CRM_Core_BAO_CustomField::getCustomFieldID( $fieldLabel_appraisedby, $groupTitle );
    $fieldLabel_appraiseraddress = 'Address of Appraiser';
    $customFieldID_appraiseraddress = CRM_Core_BAO_CustomField::getCustomFieldID( $fieldLabel_appraiseraddress, $groupTitle );
    $fieldLabel_cost = 'Original cost';
    $customFieldID_cost = CRM_Core_BAO_CustomField::getCustomFieldID( $fieldLabel_cost, $groupTitle );

    require_once 'CRM/Core/BAO/CustomValueTable.php';

    $custom_id = 'custom_' . $customFieldID_description;
    $params = array('entityID' => $contribution->id, $custom_id => 1);
    $values = CRM_Core_BAO_CustomValueTable::getValues($params);
    $inkind_values[] = $values[$custom_id];

    $custom_id = 'custom_' . $customFieldID_appraisedby;
    $params = array('entityID' => $contribution->id, $custom_id => 1);
    $values = CRM_Core_BAO_CustomValueTable::getValues($params);
    $inkind_values[] = $values[$custom_id];

    $custom_id = 'custom_' . $customFieldID_appraiseraddress;
    $params = array('entityID' => $contribution->id, $custom_id => 1);
    $values = CRM_Core_BAO_CustomValueTable::getValues($params);
    $inkind_values[] = $values[$custom_id];

    $custom_id = 'custom_' . $customFieldID_cost;
    $params = array('entityID' => $contribution->id, $custom_id => 1);
    $values = CRM_Core_BAO_CustomValueTable::getValues($params);
    $inkind_values[] = $values[$custom_id];
  }

  $receipt = array(
    'receipt_no' => $receipt_no,
    'issued_on' =>  (int) $_SERVER['REQUEST_TIME'],
    'contact_id' => $contribution->contact_id,
    'receipt_amount' => cdntaxreceipts_eligibleAmount($contribution->id),
    'is_duplicate' => $is_duplicate,
    'issue_type' => 'single',
    'issue_method' => $method,
    'receive_date' => $contribution->receive_date,
    'inkind_values' => $inkind_values,
    'receipt_status' => ($is_duplicate && isset($receipt)) ? $receipt['receipt_status'] : 'issued',
  );

  if ( $contribution->source != '' ) {
    $receipt['source'] = 'Source: ' . $contribution->source;
  }

  $receipt['contributions'] = array(
    array(
      'contribution_id' => $contribution->id,
      'contribution_amount' => $contribution->total_amount,
      'receipt_amount' => $receipt['receipt_amount'],
      'receive_date' => $contribution->receive_date,
    )
  );

  return cdntaxreceipts_processTaxReceipt($receipt, $collectedPdf, $previewMode);
}

function cdntaxreceipts_eligibleForReceipt( $contributionId ) {

  // This function may be called multiple times throughout a request lifecycle, for the
  // same contribution. This can be resource intensive, since it must load the Contribution
  // object each time and will invoke any custom hooks, which may themselves hit the DB. This
  // adds up quickly, particularly during bulk runs. The basic eligibility of a
  // contribution will not change through the request lifecycle, so we can cache those we
  // have already computed.
  static $cache = array();
  if ( array_key_exists($contributionId, $cache) ) {
    return $cache[$contributionId];
  }

  require_once('CRM/Contribute/DAO/Contribution.php');
  $contribution =  new CRM_Contribute_DAO_Contribution();
  $contribution->id = $contributionId;

  if ( ! $contribution->find( TRUE ) ) {
    CRM_Core_Error::fatal( "CDNTaxReceipts: Could not retrieve details for this contribution" );
  }

  // 1. check that contribution amount is deductible
  $deductibleAmount = cdntaxreceipts_eligibleAmount($contributionId);;

  // 2. we used to check here the financial type -- to see if it is deductible.
  // Since the introduction of split transactions (4.5 is the cleanest starting point, though they existed prior),
  // we no longer check the financial type of the contribution, and check instead the tax eligibility of each
  // line item. This is done in the eligibleAmount() function.

  // 3. check if the Contribution Status is Completed ( i.e. = 1 )
  $contributionStatus = $contribution->contribution_status_id;
  if ( !isset($contributionStatus) ) {
    CRM_Core_Error::fatal( "CDNTaxReceipts: Could not find status of this Contribution." );
  }

  // invoke hook_cdntaxreceipts_eligible:
  //   module implementations should return an array containing a single item,
  //   either TRUE or FALSE. assumes TRUE until we receive a FALSE. one FALSE
  //   disqualifies the contribution for a tax receipt.
  if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contribution, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_eligible');
  } else {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contribution, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_eligible');
  }
  $hookOk = TRUE;

  if (is_array($results)) {
    foreach ( $results as $result ) {
      if ( $result == FALSE ) {
        $hookOk = FALSE;
      }
    }
  }

  if ( $contributionStatus == '1' && $contribution->is_test == 0 && $deductibleAmount > 0 && $hookOk ) {
    $cache[$contributionId] = TRUE;
  }
  else {
    $cache[$contributionId] = FALSE;
  }

  return $cache[$contributionId];

}

function cdntaxreceipts_eligibleAmount( $contributionId ) {

  // This function may be called multiple times throughout a request lifecycle, for the
  // same contribution. This can be resource intensive, since it must load the Contribution
  // object each time and will invoke any custom hooks, which may themselves hit the DB. This
  // adds up quickly, particularly during bulk runs. The basic eligibility amount of a
  // contribution will not change through the request lifecycle, so we can cache those we
  // have already computed.
  static $cache = array();
  if ( array_key_exists($contributionId, $cache) ) {
    return $cache[$contributionId];
  }

  require_once('CRM/Contribute/DAO/Contribution.php');
  $contribution =  new CRM_Contribute_DAO_Contribution();
  $contribution->id = $contributionId;

  if ( ! $contribution->find( TRUE ) ) {
    CRM_Core_Error::fatal( "CDNTaxReceipts: Could not retrieve details for this contribution" );
  }

  // 1. calculate deductible amount
  $deductibleAmount = 0;
  if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    // compute for overall contribution in 4.4
    $financialType =  _cdntaxreceipts_get_type_for_contribution($contribution);
    if ( $financialType->is_deductible ) {
      $deductibleAmount = $contribution->total_amount;
    }
  }
  else {
    // compute by line item in 4.5+
    $line_item = new CRM_Price_DAO_LineItem();
    $line_item->contribution_id = $contributionId;
    $line_item->find();

    while ( $line_item->fetch() ) {
      $financialType =  _cdntaxreceipts_get_type_for_line_item($line_item);
      if ( $financialType->is_deductible ) {
        $deductibleAmount += $line_item->line_total;
      }
    }
  }

  // account for manually-entered non_deductible_amount
  $deductibleAmount = $deductibleAmount - $contribution->non_deductible_amount;
  if ( $deductibleAmount < 0 ) {
    $deductibleAmount = 0;
  }

  // 2. allow modules to alter the amount. lowest amount wins.
   if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contribution, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_eligibleAmount');
  } else {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contribution, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_eligibleAmount');
  }

  if (is_array($results)) {
    foreach ( $results as $result ) {
      if ( $result < $deductibleAmount ) {
        $deductibleAmount = $result;
      }
    }
  }

  $cache[$contributionId] = $deductibleAmount;
  return $deductibleAmount;
}

/**
 * issueAnnualTaxReceipt()
 * Issues an annual tax receipt for a given year.
 * Detects if the receipt is a duplicate and reacts appropriately.
 */
function cdntaxreceipts_issueAnnualTaxReceipt( $contactId, $year, &$collectedPdf = NULL, $previewMode = FALSE ) {

  // determine the send method
  list( $method, $email ) = cdntaxreceipts_sendMethodForContact($contactId);

  // query for duplicate receipt
  list( $issued_on, $receipt_id ) = cdntaxreceipts_annual_issued_on($contactId, $year);

  // When returning issued_on field:
  //   returns false for non existing $contribution ID
  //   returns 0 for existing but old data
  //   returns timestamp for new data
  $is_duplicate = empty($issued_on) ? 0 : 1;

  if ( $is_duplicate ) {
    // issue an identical receipt
    $receipt = cdntaxreceipts_load_receipt($receipt_id);
    // Cancelled receipts are not sent via email
    if ($receipt['receipt_status'] == 'cancelled') {
      $method = 'print';
    }
    $receipt['issued_on'] = (int) $_SERVER['REQUEST_TIME'];
    $receipt['is_duplicate'] = 1;
    $receipt['receive_date'] = $year;
    $receipt['issue_method'] = $method;
  }
  else {

    $contributions = cdntaxreceipts_contributions_not_receipted($contactId, $year);

    if ( count($contributions) == 0 ) {
      return array(FALSE, $method, NULL);
    }

    // calculate total amount
    $receiptContributions = array();
    $totalReceipt = 0;
    foreach ( $contributions as $c ) {
      $eligibleAmount = cdntaxreceipts_eligibleAmount($c['contribution_id']);
      $receiptContributions[] = array(
        'contribution_id' => $c['contribution_id'],
        'contribution_amount' => $c['total_amount'],
        'receipt_amount' => $eligibleAmount,
        'receive_date' => $c['receive_date'],
      );
      $totalReceipt += $eligibleAmount;
    }

    // generate a receipt number
    $receiptNo = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_prefix') . str_pad($receiptContributions[0]['contribution_id'], 8, 0, STR_PAD_LEFT);

    $receipt = array(
      'receipt_no' => $receiptNo,
      'issued_on' =>  (int) $_SERVER['REQUEST_TIME'],
      'contact_id' => $contactId,
      'receipt_amount' => $totalReceipt,
      'is_duplicate' => 0,
      'issue_type' => 'annual',
      'issue_method' => $method,
      'receive_date' => $year,
      'receipt_status' => 'issued',
      'contributions' => $receiptContributions,
    );

  }

  return cdntaxreceipts_processTaxReceipt($receipt, $collectedPdf, $previewMode);
}

/**
 * cdntaxreceipts_issueAggregateTaxReceipt()
 * Issues an grouped tax receipt for a given contact, year and list of selected contributions.
 * Currently does not issue duplicates. Pass only original contribution ids.
 *
 * @param $contactId
 * @param $year
 * @param $contributions
 * @param $method
 * @param null $collectedPdf
 * @param bool $previewMode
 * @return array
 */
function cdntaxreceipts_issueAggregateTaxReceipt($contactId, $year, $contributions, $method, &$collectedPdf = NULL,
                                                 $previewMode = FALSE ) {
  if ( count($contributions) == 0 ) {
    return array(FALSE, $method, NULL);
  }

  /** Two different formats come into this function in the $contributions array
   * 1. When issuing for the first time:
   * array ($contribution_id => array(
   *   'contribution_id' => $id,
   *   'contact_id' => $dao->contact_id,
   *   'total_amount' => $dao->total_amount,
   *   'non_deductible_amount' => $dao->non_deductible_amount,
   *   'receive_date' => $dao->receive_date,
   *   'receive_year' => $dao->receive_year,
   *   'eligible' => $eligible,
   *   'receipt_id' => $dao->receipt_id,
   * ),);
   * 2. When re-issuing:
   *
   *  $index (not contrib id) => array(
   *    'contribution_id'
   *    'contribution_amount'
   *    'receipt_amount'
   *    'receive_date'
   * );
   **/

  $contrib_copy = $contributions;
  $check_contribution = array_shift($contrib_copy);
  list($issued_on, $receipt_id) = cdntaxreceipts_issued_on($check_contribution['contribution_id']);

  if (isset($check_contribution['receipt_id']) && $check_contribution['receipt_id'] != 0) {
    $is_duplicate = TRUE;
  }
  else {
    $is_duplicate = empty($issued_on) ? 0 : 1;
  }

  if ( $is_duplicate ) {
    $receipt = cdntaxreceipts_load_receipt($receipt_id);
    // Cancelled receipts are not sent via email
    if ($receipt['receipt_status'] == 'cancelled') {
      $method = 'print';
    }
    $receipt['issued_on'] = (int) $_SERVER['REQUEST_TIME'];
    $receipt['is_duplicate'] = 1;
    $receipt['issue_method'] = $method;
    $receipt['receive_date'] = $year;
  }
  else {
    // calculate total amount
    $receiptContributions = array();
    $totalReceipt = 0;
    foreach ( $contributions as $c ) {
      $eligibleAmount =  cdntaxreceipts_eligibleAmount($c['contribution_id']);
      $receiptContributions[] = array(
        'contribution_id' => $c['contribution_id'],
        'contribution_amount' => $c['total_amount'],
        'receipt_amount' => $eligibleAmount,
        'receive_date' => $c['receive_date'],
      );
      $totalReceipt += $eligibleAmount;
    }

    // generate a receipt number
    $receiptNo = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'receipt_prefix') . str_pad($receiptContributions[0]['contribution_id'], 8, 0, STR_PAD_LEFT);

    $receipt = array(
      'receipt_no' => $receiptNo,
      'issued_on' =>  (int) $_SERVER['REQUEST_TIME'],
      'contact_id' => $contactId,
      'receipt_amount' => $totalReceipt,
      'is_duplicate' => 0,
      'issue_type' => 'aggregate',
      'issue_method' => $method,
      'receive_date' => $year,
      'receipt_status' => 'issued',
      'contributions' => $receiptContributions,
    );
  }

  return cdntaxreceipts_processTaxReceipt($receipt, $collectedPdf, $previewMode);
}


/* determines the send method for the specified contact
 * returns array, [0] = 'email' or 'print', [1] = email address or NULL
 */
function cdntaxreceipts_sendMethodForContact( $contactId ) {

  $global_email = CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'enable_email', NULL, TRUE);
  if ( ! $global_email ) {
    return array('print', NULL);
  }

  $method = array('print', NULL);

  require_once 'CRM/Contact/BAO/Contact.php';
  list($displayname, $email, $doNotEmail, $onHold) = CRM_Contact_BAO_Contact::getContactDetails($contactId);

  if ( isset($email) ) {
    if ( ! $doNotEmail && ! $onHold ) {
      $method = array('email', $email);
    }
  }

  // invoke hook_cdntaxreceipts_method:
  //   module implementations should return an array containing a single item,
  //   either 'email' or 'print'. if we receive one or more 'print', the contact
  //   is disqualified from receiving email receipts.
  if (version_compare(CRM_Utils_System::version(), '4.5', '<')) {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contactId, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_method');
  } else {
    $results = CRM_Utils_Hook::singleton()->invoke(1, $contactId, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, CRM_Utils_Hook::$_nullObject, 'cdntaxreceipts_method');
  }
  $emailOk = TRUE;

  if (is_array($results)) {
    foreach ( $results as $result ) {
      if ( $result == 'print' ) {
        $method = array('print', NULL);
      }
    }
  }

  return $method;

}

function cdntaxreceipts_sendMethodForContribution( $contributionId ) {

  $contribution =  new CRM_Contribute_DAO_Contribution();
  $contribution->id = $contributionId;

  if ( ! $contribution->find( TRUE ) ) {
    CRM_Core_Error::fatal( "CDNTaxReceipts: Could not retrieve details for this contribution" );
  }

  return cdntaxreceipts_sendMethodForContact( $contribution->contact_id );
}

function cdntaxreceipts_openCollectedPDF() {

  static $pdf;

  if ( ! isset($pdf) ) {
    //define ('K_PATH_IMAGES', '');
    require_once 'tcpdf/tcpdf.php';

    $pdf = new PDF(PDF_PAGE_ORIENTATION, PDF_UNIT, 'LETTER', TRUE, 'UTF-8', FALSE);
    $pdf->Open();

    $pdf->SetAuthor(CRM_Core_BAO_Setting::getItem(CDNTAX_SETTINGS, 'org_name'));

    $mymargin_left = 12;
    $mymargin_top = 6;
    $mymargin_right = 12;
    $pdf->SetMargins($mymargin_left, $mymargin_top, $mymargin_right);

    $pdf->setJPEGQuality('100');

    $pdf->SetAutoPageBreak('', $margin=0);
  }

  return $pdf;

}

function cdntaxreceipts_sendCollectedPDF(&$pdf, $filename) {
  if ( $pdf->getNumPages() > 0 ) {
    $pdf->Output($filename, 'D');
    CRM_Utils_System::civiExit();
  }
  else {
    $pdf->Close();
  }
}

/**
 * Isolate version checking code
 * @param $contribution
 * @return CRM_Contribute_DAO_ContributionType|CRM_Financial_DAO_FinancialType
 */
function _cdntaxreceipts_get_type_for_contribution($contribution) {
  // Require once lines are only needed if the civicrm.settings.php does not have the autoloader line
  require_once 'CRM/Utils/VersionCheck.php';
  $version_checker = CRM_Utils_VersionCheck::singleton();
  $majorVersion = floatval(substr($version_checker->localVersion, 0, 3));
  if ($majorVersion >= 4.3) {
    require_once 'CRM/Financial/DAO/FinancialType.php';
    $contributionType = new CRM_Financial_DAO_FinancialType();
    $contributionType->id = $contribution->financial_type_id;
    if ( ! $contributionType->find( TRUE ) ) {
      CRM_Core_Error::fatal( "CDNTaxReceipts: Could not find corresponding contribution type." );
    }
  }
  else { // Assume 4.2 or lower
    require_once 'CRM/Contribute/DAO/ContributionType.php';
    $contributionType = new CRM_Contribute_DAO_ContributionType();
    $contributionType->id = $contribution->contribution_type_id;
    if ( ! $contributionType->find( TRUE ) ) {
      CRM_Core_Error::fatal( "CDNTaxReceipts: Could not find corresponding contribution type." );
    }
  }
  return $contributionType;
}

function _cdntaxreceipts_get_contribution_dao() {
  // Require once lines are only needed if the civicrm.settings.php does not have the autoloader line
  require_once 'CRM/Utils/VersionCheck.php';
  $version_checker = CRM_Utils_VersionCheck::singleton();
  $majorVersion = floatval(substr($version_checker->localVersion, 0, 3));
  if ($majorVersion >= 4.3) {
    require_once 'CRM/Financial/DAO/FinancialType.php';
    $contributionType = new CRM_Financial_DAO_FinancialType();
  }
  else { // Assume 4.2 or lower
    require_once 'CRM/Contribute/DAO/ContributionType.php';
    $contributionType = new CRM_Contribute_DAO_ContributionType();
  }
  return $contributionType;
}

function _cdntaxreceipts_get_type_for_line_item($line_item) {
  require_once 'CRM/Financial/DAO/FinancialType.php';
  $financialType = new CRM_Financial_DAO_FinancialType();
  $financialType->id = $line_item->financial_type_id;
  if ( ! $financialType->find( TRUE ) ) {
    CRM_Core_Error::fatal( "CDNTaxReceipts: Could not find corresponding contribution type." );
  }
  return $financialType;
}

/**
 * Get the correct contact address. Get the billing address followed by the is_primary as a fallback.
 * @param $contact_id
 * @return array|mixed|null address : Contact address fixed with named country and state_province
 */
function cdntaxreceipts_getAddress($contact_id) {

  $address = NULL;
  // get Address information via contact
  $params = array(
    'version' => 3,
    'contact_id' => $contact_id,
    'is_billing' => 1,
  );


  $address_results = civicrm_api('Address', 'get', $params);

  if ( $address_results['is_error'] == 0) {
    $address = array_shift($address_results['values']);
  }

  if (!isset($address)) {
    $params = array(
      'version' => 3,
      'contact_id' => $contact_id,
      'is_primary' => 1,
    );
    $address_results = civicrm_api('Address', 'get', $params);

    if ( $address_results['is_error'] == 0) {
      $address = array_shift($address_results['values']);
    }

  }
  $address = isset($address) ? $address : array();
  // add actual names for province and country instead of just having ids
  require_once 'CRM/Core/BAO/Address.php';
  CRM_Core_BAO_Address::fixAddress($address);

  return $address;
}

/**
 * @param $issue_type
 * @return string display formatted Issue type
 */
function _cdntaxreceipts_get_display_type($issue_type) {
  switch ($issue_type) {
    case 'aggregate' :
      return 'Aggregate';
    case 'annual' :
      return 'Annual';
    case 'single' :
      return 'Single';
    default:
      return '';
  }
}

