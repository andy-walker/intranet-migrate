<?php

/**
 * Intranet import script - andyw@circle, 16/12/2013
 * Import content types such as support tickets, sites, todos 
 * into new Drupal 7 intranet
 */

define('IMPORT_INTRANET_USERS_CSV_FILE', '/home/andyw/users.csv');
define('IMPORT_INTRANET_CLIENT_SUPPORT_RID', 7);

define('IMPORT_INTRANET_SITES_VID', 22);

define('IMPORT_INTRANET_LIVE_SITE_PARENT_TID', 889);
define('IMPORT_INTRANET_TEST_SITE_PARENT_TID', 891);
define('IMPORT_INTRANET_DEV_SITE_PARENT_TID',  890);

function import_intranet_drush_command() {  
    
    return array(
        'import-intranet' => array(
            'description' => dt("Internal script for intranet D7 import"),
            'arguments'   => array(),
            'options'     => array(),
            'examples'    => array(),
            'bootstrap'   => DRUSH_BOOTSTRAP_DRUPAL_FULL
        )
    );

}

function import_intranet_drush_help($section) {
    
    switch ($section) {
        case 'drush:import-intranet':
            return dt("Used to import data into D7 intranet, exported with the export-intranet script");
    }

}

/**
 * Main import class - this handles the specifics of each content_type import
 */
class IntranetImport {
    
    private $nid_before;

    public function __construct() {
        
        // Create import mapping table if not already created
        /*
        db_query("
            CREATE TABLE IF NOT EXISTS `circle_import_mapping` (
              `src_nid` int(10) unsigned NOT NULL,
              `src_url` varchar(255) NULL,
              `type` varchar(32) NOT NULL,
              `dst_nid` int(10) unsigned NOT NULL,
              PRIMARY KEY (`src_nid`)
            ) ENGINE=MyISAM DEFAULT CHARSET=latin1 
              COMMENT='Table to map old node ids to new ones, for redirection';
        ");
        */
    }

    // For each 'site', create a corresponding client
    public function clients() {
        
        // query all existing site nodes from the Drupal db
        foreach (db_query("SELECT nid FROM node WHERE type = 'site'") as $source) {

            $site = node_load($source->nid);
            $path = drupal_lookup_path('alias', 'node/' . $source->nid);
            
            // construct D7 node object
            $node = new StdClass;
            $node->title    = $site->title;
            $node->uid      = $site->uid;
            $node->type     = 'client';
            $node->language = 'und';

            // set field_client_sites node ref field with the node id of the site
            $node->field_client_sites['und'][0]['nid'] = $source->nid;
            
            // save the new node
            node_save($node);
            drush_log('Inserted client node: ' . $node->title, 'ok');

            // also set field_client node ref on the site with the node id of the client
            $site->field_client['und'][0]['nid'] = $node->nid;

            // create path for the client page which is the same as the site one, except
            // prefixed with 'client' instead of 'site'
            if ($path) {

                $client_path    = explode('/', $path);
                $client_path[0] = 'client';
                $client_path    = implode('/', $client_path);

                $path_info = array(
                    'source'   => "node/{$node->nid}",
                    'alias'    => $client_path,
                    'language' => 'und'               
                );

                path_save($path_info);

            } else {
                drush_log('No site path found when importing node ' . $source->nid, 'warning');
            }

            // save the site node (updating field_client) - do this down here otherwise $site->path
            // will get destroyed before we need to use it
            node_save($site);

            // create mapping table entry to allow future redirects
            //$this->map('client', $source->nid, $node->nid);

        }

    }

    public function comments() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            //'status'   => 1
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'pid', 'nid', 'uid', 'subject', 'status', 'hostname', 'name', 'mail', 'homepage'
        );

        $max_cid = db_query("SELECT MAX(cid) FROM comment")->fetchField();
      
        foreach (db_query("SELECT * FROM circle_import_data WHERE entity_type = 'comment'") as $source) {

            // unserialize existing D6 node data
            $source = unserialize($source->data);
            
            // construct D7 node object
            $comment = new StdClass;

            // attach static and unchanged properties
            foreach ($properties_static as $property => $value)
                $comment->$property = $value;

            foreach ($properties_unchanged as $property)
                $comment->$property = $source->$property;           

            // timestamp -> created / changed
            $comment->created = $source->timestamp;
            $comment->changed = $source->timestamp;

            // convert body to D7 format
            $comment->comment_body['und'][0] = array(
                'value'  => $source->comment,
                'format' => $source->format
            );

            //$comment->is_new = !$this->comment_exists($comment->cid);

            // determine / set node type
            switch (node_load($comment->nid)->type) {
                case 'support_ticket':
                    $comment->node_type = 'comment_node_support_ticket';
                    break;
                case 'todo':
                    $comment->node_type = 'comment_node_todo';
                    break;
                default:
                    drush_log('Unable to determine node type for comment id: ', 'warning');
            }

            drush_log('Inserting comment: ' . $comment->subject, 'ok');
            //echo "Before: ";
            //print_r($comment);
            comment_submit($comment);
            comment_save($comment);
            //echo "After: ";
            //print_r($comment);

        }

        db_query("UPDATE comment SET status = 1 WHERE cid > " . $max_cid);
        
    }

    // Main import functions //

    public function notes() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und'
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'nid', 'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:
        $properties_changed = array();

        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'notes'") as $source) {
            
            // unserialize existing D6 node data
            $source = unserialize($source->data);
            
            // construct D7 node object
            $node   = new StdClass;
            
            foreach ($properties_static as $property => $value)
                $node->$property = $value;

            foreach ($properties_unchanged as $property)
                $node->$property = $source->$property;
            
            foreach ($properties_changed as $property)
                $node->$property = array('und' => $source->$property);

            $node->is_new = !$this->node_exists($node->nid);

            if (trim($source->body)) {
                $node->body['und'][0]['value']  = $source->body;
                $node->body['und'][0]['format'] = 4;
            }

            // convert og content field to D7 format ..
            foreach ($source->og_groups as $gid)
                $node->og_group_ref['und'][] = array('target_id' => $gid);

            // transfer any existing taxonomy terms
            foreach ($source->taxonomy as $tid => $term) {
                $destination_field = 'taxonomy_vocabulary_' . $term->vid;
                $node->{$destination_field}['und'][] = array(
                    'tid' => $tid
                );
            }
            
            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' notes node: ' . $node->title, 'ok');

            // save new node
            node_save($node);

            // path alias stuff
            if ($source->path) {
                // create path for the new og which is the same as the old one
                $path_info = array(
                    'source'   => "node/{$node->nid}",
                    'alias'    => $source->path,
                    'language' => 'und'               
                );
                path_save($path_info);
            } 

        }

    }

    // Import 'site' content type
    public function sites() {
        
        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und'
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'nid', 'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture',
            'og_selective', 'og_description', 'og_theme', 'og_register', 'og_directory',
            'og_language', 'og_private'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:     
        $properties_changed = array(
            'field_aliases', 'field_content_site', 'field_dns_control', 'field_site_notes',
            'field_repo_name', 'field_svn', 'field_civi_custom', 'field_support_hours',
            'field_defunct_date', 'field_nameservers', 'field_live_server',
            'field_project_manager', 'field_project_director', 'field_maintain_site',
            'field_site_notes', 'field_support_hours', 'field_external_maintainer',
            'field_cease_hosting', 'field_hosting_rate', 'field_support_rate',
            'field_has_custom_php', 'field_has_custom_templates'
        );
        
        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'site'") as $source) {
            
            // unserialize existing D6 node data
            $source = unserialize($source->data);
            
            // construct D7 node object
            $node   = new StdClass;
            
            foreach ($properties_static as $property => $value)
                $node->$property = $value;

            foreach ($properties_unchanged as $property)
                $node->$property = $source->$property;
            
            foreach ($properties_changed as $property)
                $node->$property = array('und' => $source->$property);
            
            $node->is_new = !$this->node_exists($node->nid);

            // there is no body field on new site - append to field_notes if populated
            if (trim($source->body)) {
                if (trim(@$node->field_notes['und'][0]['value']))
                    $node->field_notes['und'][0]['value'] .= '<br /><br />';
                $node->field_notes['und'][0]['value'] .= $source->body;
                $node->field_notes['und'][0]['format'] = 4;
            }

            // append og_description to field_notes when og_description is not empty
            if (trim($source->og_description)) {
                $node->field_notes['und'][0]['value'] .= (trim($source->body) ? "\n\n" : '') . $source->og_description;
                $node->field_notes['und'][0]['format'] = 4;
            }

            // create taxonomy terms for live, test and dev + assign to node
            foreach (array(
                
                'field_livesite' => IMPORT_INTRANET_LIVE_SITE_PARENT_TID,
                'field_testsite' => IMPORT_INTRANET_TEST_SITE_PARENT_TID,
                'field_devsite'  => IMPORT_INTRANET_DEV_SITE_PARENT_TID

            ) as $field => $parent_tid) {

                if (isset($source->{$field}[0]['value']) and !empty($source->{$field}[0]['value'])) {    
                    
                    $term         = new StdClass;
                    $term->name   = $source->{$field}[0]['value'];
                    $term->vid    = IMPORT_INTRANET_SITES_VID;
                    $term->parent = $parent_tid;
                    //echo "Saving term: " . print_r($term, true);
                    taxonomy_term_save($term);
                    $node->field_url_type['und'][] = array('tid' => $term->tid);

                }

            }

            // transfer any existing taxonomy terms
            foreach ($source->taxonomy as $tid => $term) {
                $destination_field = 'taxonomy_vocabulary_' . $term->vid;
                $node->{$destination_field}['und'][] = array(
                    'tid' => $tid
                );
            }
            
            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' site node: ' . $node->title, 'ok');
            node_save($node);
            //og_insert_group($node);
            
            // reassign og membership from old group to this group
            // drush_log("Reassigning membership from gid {$source->nid} to gid {$node->nid}", 'ok');
            
            // don't do this for now - we'll do this at the end
            // $this->og_reassign_membership($source->nid, $node->nid);

            // delete old og - this should free up the url alias <crosses fingers>, and avoid confusion between old and new
            // node_delete($source->nid);
            // drush_log('Deleted old og, nid: ' . $source->nid, 'ok');

            if ($source->path) {
                // create path for the new og which is the same as the old one
                $path_info = array(
                    'source'   => "node/{$node->nid}",
                    'alias'    => $source->path,
                    'language' => 'und'               
                );
                path_save($path_info);
            } else {
                drush_log('No site path found when importing node ' . $source->entity_id, 'warning');   
            }

            // create mapping table entry to allow future redirects
            //$this->map('site', $source->path, $source->nid, $node->nid);

        }

        //drush_log('Deleting duplicate og_memberships', 'ok');

        // For any duplicated 'user' entries in og_membership, delete the latest one

        // retrieve duplicated entries
        /*
        $results = db_query("
            SELECT ogm2.* FROM og_membership ogm1
            INNER JOIN og_membership ogm2 
            ON ogm1.entity_type = ogm2.entity_type
            AND ogm1.gid = ogm2.gid
            AND ogm1.etid = ogm2.etid 
            AND ogm1.id != ogm2.id
            ORDER BY ogm2.created DESC
        ");
        
        // put into an array keyed by <entity_type>-<entity id>-<group id> - so with ORDER BY ogm2.created DESC, the earliest entry will
        // always overwrite the later one 
        $memberships = array();
        foreach ($results as $result) {
            $key = $result->entity_type . '-' . $result->etid . '-' . $result->gid;
            $memberships[$key] = $result->id;
        }

        // then delete all rows where the id is in $memberships
        db_query("DELETE FROM og_membership WHERE id IN (" . implode(',', $memberships) . ")");

        drush_log('Deleted ' . count($memberships) . ' memberships', 'ok');
        */

    }

    public function support_tickets() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            'status'   => 1
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'nid', 'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:
        $properties_changed = array();

        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'support_ticket'") as $source) {

            // unserialize existing D6 node data
            if (!$source = unserialize($source->data)) {
                drush_log('Failed to deserialize - skipping node', 'warning');
                continue;
            }
            
            // construct D7 node object
            $node   = new StdClass;
            
            foreach ($properties_static as $property => $value)
                $node->$property = $value;

            foreach ($properties_unchanged as $property)
                $node->$property = $source->$property;
            
            foreach ($properties_changed as $property)
                $node->$property = array('und' => $source->$property);

            if (trim($source->body)) {
                $node->body['und'][0]['value']  = $source->body;
                $node->body['und'][0]['format'] = $source->format;
            }

            $node->is_new = !$this->node_exists($node->nid);

            // todo: taxonomy, additional fields ..

            // create mapping table entry to allow future redirects
            //$this->map('support_ticket', $source->path, $source->nid, $node->nid);

            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' support ticket node: ' . $node->title, 'ok');

            node_save($node);

        }


    }

    public function todos() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und'
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:
        $properties_changed = array();

        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'todo'") as $source) {
            
            // unserialize existing D6 node data
            $source = unserialize($source->data);
            
            // construct D7 node object
            $node   = new StdClass;
            
            foreach ($properties_static as $property => $value)
                $node->$property = $value;

            foreach ($properties_unchanged as $property)
                $node->$property = $source->$property;
            
            foreach ($properties_changed as $property)
                $node->$property = array('und' => $source->$property);

            $node->is_new = !$this->node_exists($node->nid);

            if (trim($source->body)) {
                $node->body['und'][0]['value']  = $source->body;
                $node->body['und'][0]['format'] = $source->format;
            }

            // todo: additional fields, taxonomy etc

            // create mapping table entry to allow future redirects
            //$this->map('todo', $source->path, $source->nid, $node->nid);
            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' todo node: ' . $node->title, 'ok');
            node_save($node);

        }

        // todo: import comments!
    }

    /**
     * Import users from csv file
     */
    public function users() {
        if (($handle = fopen(IMPORT_INTRANET_USERS_CSV_FILE, "r")) !== false) {
            while (($data = fgetcsv($handle, 1000, ",")) !== false) {
                db_query("
                    REPLACE INTO users (uid, name, mail)
                    VALUES ('{$data[0]}', '{$data[1]}', '{$data[2]}')
                ");
                // add client-support role
                db_query("
                    REPLACE INTO users_roles (uid, rid)
                    VALUES ('{$data[0]}', " . IMPORT_INTRANET_CLIENT_SUPPORT_RID . ")
                ");
            }
            fclose($handle);
        }
    }

    // Private (helper) functions //

    /**
     * Given the source term id, import term from import data
     */
    /*
    private function import_term($source_tid) {
        // get term
        $term = unserialize(db_query("
            SELECT data FROM circle_import_data WHERE entity_type = 'term' AND entity_id = $source_tid
        ")->fetchField());

        // todo: import with same tid - think this will involve disabling auto_increment, inserting with
        // sql, and setting auto_increment to max(tid) + 1 at the end.
    }
    */

    private function comment_exists($cid) {
        return (bool)db_query("SELECT cid FROM comment WHERE cid = :cid", array(':cid' => $cid))->fetchField();
    }

    private function node_exists($nid) {
        return (bool)db_query("SELECT nid FROM node WHERE nid = :nid", array(':nid' => $nid))->fetchField();
    }

    /**
     * Create mapping table entry for redirecting old nid (or old url) to new url
     */
    /*
    private function map($node_type, $source_url, $source_nid, $destination_nid) {
        db_query("
            REPLACE INTO circle_import_mapping (src_nid, src_url, type, dst_nid)
            VALUES ($source_nid, '$source_url', '$node_type', $destination_nid)
        ");
    }
    */

    /**
     * Reassign all members of organic group <old nid> to <new nid>
     */
    private function og_reassign_membership($old_nid, $new_nid) {
        db_query("
            UPDATE og_membership SET gid = :new_nid WHERE gid = :old_nid
        ", array(
              ':new_nid' => $new_nid,
              ':old_nid' => $old_nid
           )
        );

    }

    /**
     * Switch path alias from node <old nid> to node <new nid>
     */
    private function path_alias_reassign($old_nid, $new_nid) {
        db_query("
            UPDATE url_alias SET source = 'node/$new_nid'
             WHERE source = 'node/$old_nid'
        ");
    }       

};

/**
 * Main command callback
 */
function drush_import_intranet() {

    $import = new IntranetImport();

    module_disable(array('pathauto', 'ci_support', 'tracker'));

    //$import->users(); // this has been run

    // do ogs first, because other things link to them
    $import->sites();
    

    //$import->notes();

    $import->support_tickets();
    $import->comments();
    //$import->todos();

    # clients should be done at the end as these are new content items - we don't
    # want to accidentally use node ids that will be needed for imported content
    //$import->clients();

    # todo 
    //$import->files();


    module_enable(array('pathauto', 'ci_support', 'tracker'));

}

