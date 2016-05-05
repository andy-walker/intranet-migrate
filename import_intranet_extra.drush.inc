<?php

/**
 * Additional intranet import script - andyw@circle, 19/12/2014
 * This is an additional script to fix post-import issues
 */

function import_intranet_extra_drush_command() {  
    
    return array(
        'import-intranet-extra' => array(
            'description' => dt("Additional script for intranet D7 import"),
            'arguments'   => array(),
            'options'     => array(),
            'examples'    => array(),
            'bootstrap'   => DRUSH_BOOTSTRAP_DRUPAL_FULL
        )
    );

}

function import_intranet_extra_drush_help($section) {
    
    switch ($section) {
        case 'drush:import-intranet':
            return dt("Used to import data into D7 intranet, exported with the export-intranet script");
    }

}

/**
 * Main command callback
 */
function drush_import_intranet_extra() {
    //drush_import_extra_fix_invoiced();
    //drush_import_extra_fix_hours_total();
    drush_import_extra_add_missing_clients();
}

function drush_import_extra_add_missing_clients() {
    $results = db_query("SELECT n.nid FROM node n LEFT JOIN ci_support_clients sc ON n.nid = sc.client_nid WHERE n.type = 'site' AND sc.client_nid IS NULL");
    foreach ($results as $result) {
        echo "Adding entry for nid {$result->nid}\n";
        db_query("INSERT INTO ci_support_clients (client_nid, autoassign, autosubscribe) VALUES ({$result->nid}, 368, 'a:0:{}')");
    }

}

function drush_import_extra_fix_invoiced() {

    $results = db_query("SELECT content_nid AS nid FROM `ci_support_content` WHERE status = 4");

    foreach ($results as $result) {
        $nid = $result->nid;
        echo "Updating $nid\n";
        db_query("UPDATE field_data_field_invoiced SET field_invoiced_value = 1 WHERE entity_type = 'node' AND entity_id = $nid");
        db_query("UPDATE field_revision_field_invoiced SET field_invoiced_value = 1 WHERE entity_type = 'node' AND entity_id = $nid");
    }

}

function drush_import_extra_fix_hours_total() {
    
    $results = db_query("SELECT nid FROM node WHERE type IN ('todo', 'support_ticket')");
    
    foreach ($results as $result) {
        
        $nid  = $result->nid;
        $node = node_load($nid);
        
        if ($node->type == 'support_ticket')
            $total = (float)db_query("
                SELECT SUM(field_hours_spent_value) FROM field_data_field_hours_spent 
                 WHERE entity_type = 'node' AND entity_id = $nid
            ")->fetchField();
        else
            $total = (float)db_query("
                SELECT SUM(field_hours_spent_todo_value) FROM field_data_field_hours_spent_todo 
                 WHERE entity_type = 'node' AND entity_id = $nid
            ")->fetchField();           
        
        print $node->type . ' ' . $nid . ': ' . $total . PHP_EOL;

        db_query("UPDATE field_data_field_hours_total SET field_hours_total_value = $total WHERE entity_type = 'node' AND entity_id = $nid");
        db_query("UPDATE field_revision_field_hours_total SET field_hours_total_value = $total WHERE entity_type = 'node' AND entity_id = $nid");
    
    }  

}