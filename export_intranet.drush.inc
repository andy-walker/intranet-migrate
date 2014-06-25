<?php

/**
 * Intranet export script
 * Export Drupal 6 content types such as support tickets, sites, todos etc into a 
 * temporary table, ready for import into new Drupal 7 system
 * NB: this script should be bootstrapped against the live D6 site
 * andyw@circle, 16/12/2013
 */

function export_intranet_drush_command() {
    
    return array(
        'export-intranet' => array(
            'description' => dt("Script to export old D6 node content into a database table"),
            'arguments'   => array(),
            'options'     => array(),
            'examples'    => array(),
            'bootstrap'   => DRUSH_BOOTSTRAP_DRUPAL_FULL
        )
    );

}

function export_intranet_drush_help($section) {
    switch ($section) {
        case 'drush:export-intranet':
            return dt("Use this script to export D6 intranet content, ready for D7 import");
    }
}

/**
 * Main command callback
 */
function drush_export_intranet() {

    ini_set('memory_limit', '1024M');

    $error = function($message) {
        drush_log($message, 'error');
        exit;
    };

    $warning = function($message) {
        drush_log($message, 'warning');
    };

    /**
     * Inner function to read in source node content from D6 db
     * and insert into temporary database table to import from
     */
    $export = function($content_types) use ($error, $warning) {
        
        civicrm_initialize();

        // pull source nids from intranet6 database
        $results = db_query("SELECT nid FROM node WHERE type IN ('" . implode("', '", $content_types) . "')");
        $current = 0;

        while ($result = db_fetch_object($results)) {
            
            // load full node from node id
            if (!$node = node_load($result->nid))
                $warning('Unable to load node ' . $result->nid);

            // insert serialized node data into circle_import_data table
            db_query("
                INSERT INTO circle_import_data (id, entity_type, entity_id, type, data) 
                     VALUES (NULL, 'node', {$node->nid}, '{$node->type}', '%s')
            ",
                serialize($node)
            );

            // output progress
            drush_log('Inserted row for node id ' . $node->nid . ' (' . $node->title . ") - record " . ++$current . "/unknown", 'ok');
        
        }

    };

    $export_comments = function() use ($error, $warning) {
        
        // pull source source comment ids from intranet6 database
        $results = db_query("SELECT cid FROM comments");
        $current = 0;
        
        while ($result = db_fetch_object($results)) {
            
            // load full node from node id
            if (!$comment = _comment_load($result->cid))
                $warning('Unable to load comment ' . $result->cid);

            // insert serialized node data into circle_import_data table
            db_query("
                INSERT INTO circle_import_data (id, entity_type, entity_id, parent_entity_id, type, data) 
                     VALUES (NULL, 'comment', {$comment->cid}, {$comment->nid}, '', '%s')
            ",
                serialize($comment)
            );

            // output progress
            drush_log('Inserted row for comment id ' . $comment->cid . ' (' . $comment->subject . ") - record " . ++$current . "/unknown", 'ok');

        } 

    };

    $export_files = function() use ($error, $warning) {

        // get file info for each file on the system
        $results = db_query("SELECT * FROM files");
        $current = 0;

        while ($file = db_fetch_object($results)) {
            // insert serialized node data into circle_import_data table
            db_query("
                INSERT INTO circle_import_data (id, entity_type, entity_id, parent_entity_id, type, data) 
                     VALUES (NULL, 'file', {$file->fid}, 0, '', '%s')
            ",
                serialize($file)
            );

            // output progress
            drush_log('Inserted row for file id ' . $file->fid . ' (' . $file->filename . ") - record " . ++$current . "/unknown", 'ok');            
        }

    };

    $export_taxonomy = function() use ($error, $warning) {
        
        // pull source source term ids from intranet6 database
        $results = db_query("SELECT tid FROM term_data");
        $current = 0;
        
        while ($result = db_fetch_object($results)) {
            
            // load full node from node id
            if (!$term = taxonomy_get_term($result->tid))
                $warning('Unable to load taxonomy term ' . $result->tid);

            // insert serialized node data into circle_import_data table
            db_query("
                INSERT INTO circle_import_data (id, entity_type, entity_id, parent_entity_id, type, data) 
                     VALUES (NULL, 'term', {$term->tid}, 0, '', '%s')
            ",
                serialize($term)
            );

            // output progress
            drush_log('Inserted row for term id ' . $term->tid . ' (' . $term->name . ") - record " . ++$current . "/unknown", 'ok');

        }

    };

    $export_og_membership = function() use ($error, $warning) {

        foreach (array('og_uid', 'og_ancestry') as $entity) {

            $rows    = db_query("SELECT * FROM $entity");
            $total   = db_fetch_object(db_query("SELECT COUNT(*) AS num_rows FROM $entity"))->num_rows;
            $current = 0;
            
            while ($row = db_fetch_object($rows)) {
                
                db_query("
                    INSERT INTO circle_import_data (id, entity_type, entity_id, parent_entity_id, type, data) 
                         VALUES (NULL, '$entity', '{$result->nid}', '{$result->group_nid}', '', '%s')
                ",
                    serialize($row)
                );               
                drush_log("Exported $entity, record $current/$total", 'ok');
       
                $current++;
            
            }

        }

    };

    $export_users = function() use ($error, $warning) {

        // get file info for each file on the system
        $results = db_query("SELECT * FROM users");
        $current = 0;

        while ($user = db_fetch_object($results)) {
            // insert serialized node data into circle_import_data table
            db_query("
                INSERT INTO circle_import_data (id, entity_type, entity_id, parent_entity_id, type, data) 
                     VALUES (NULL, 'user', {$user->uid}, 0, '', '%s')
            ",
                serialize($user)
            );

            // output progress
            drush_log('Inserted row for user id ' . $user->uid . ' (' . $user->name . ") - record " . ++$current . "/unknown", 'ok');            
        }

    };

    // create table to hold the data
    db_query("DROP TABLE IF EXISTS circle_import_data");
    db_query("
        CREATE TABLE `circle_import_data` (
          `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
          `entity_type` varchar(32) NOT NULL,
          `entity_id` int(11) unsigned NOT NULL,
          `parent_entity_id` int(11) unsigned NULL,
          `type` varchar(32) NOT NULL,
          `data` longtext NOT NULL,
          PRIMARY KEY (`id`),
          KEY `type` (`type`)
        ) ENGINE=InnoDB DEFAULT CHARSET=latin1 COMMENT='Table to export D6 node data into' AUTO_INCREMENT=1 ;
    ");

    // export user
    $export_users();

    // export content
    
    $export(array(
        'todo', 
        'support_ticket',
        'site',
        'client',
        'notes'
    ));

    // export comments and taxonomy terms
    $export_comments();
    $export_og_membership();
    $export_taxonomy();
    $export_files();

    ini_restore('memory_limit');

}