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

define('IMPORT_INTRANET_START_NODE', 8495);

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
    private $support_assigned;
    private $support_subscribed;

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

    public function assignSupportContent() {

        $results = db_query("SELECT nid FROM node WHERE type IN ('support_ticket')");
        foreach ($results as $result) {
            # may not perform this check in future - ie: update all
            //if (!$this->hasSupportContentEntity($result->nid)) {
                drush_log('Replacing content entity for node ' . $result->nid, 'ok');
                $this->createSupportContentEntity($result->nid);
            //} else 
            //    drush_log('Support content entity already exists for node ' . $result->nid . '. Skipping record.', 'ok');
        }
    

    }

    public function assignTodoContent() {

        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'todo'") as $source) {
            
            // unserialize existing D6 node data
            if (!$source = unserialize($source->data)) {
                drush_log('Unable to unserialize node data', 'warning');
                continue;
            }

            // create ci_support_content entity for the todo
            if ($content_nid = $source->nid) {
                
                if (!$group_nid = $this->getContentGroup($content_nid)) {
                    drush_log('Unable to get group nid for todo ' . $content_nid, 'warning');
                    continue;                    
                }
                
                if (!$client_nid = $this->getClientForSite($group_nid)) {
                    drush_log('Unable to get client nid for group nid ' . $group_nid, 'warning');
                    continue;
                }

                $assigned   = $source->field_assigned[0]['uid'];
                $subscribed = array(); // no subscriptions on D6 todos

                $complete = 0;
                $status   = 2; // active

                if (isset($source->field_status[0]['value']) and $source->field_status[0]['value'] == 'closed')
                    $complete = 1;

                if ($complete)
                    $status = 4; // closed

                db_query("
                    REPLACE INTO ci_support_content (content_nid, client_nid, assigned, subscribed, status, priority, complete)
                          VALUES (:content_nid, :client_nid, :assigned, :subscribed, :status, :priority, :complete)
                ", array(
                      ':content_nid' => $content_nid,
                      ':client_nid'  => $group_nid,
                      ':assigned'    => $assigned ? $assigned : 350, // assign to maya if no one assigned
                      ':subscribed'  => serialize($subscribed),
                      ':status'      => $status,
                      ':priority'    => 2,
                      ':complete'    => $complete
                   )
                );
            
            } else drush_log('Error: no node id', 'error');

        }        

    }

    // For each 'site', create a corresponding client
    public function clients() {
        
        // query all existing site nodes from the Drupal db
        foreach (db_query("SELECT nid FROM node WHERE type = 'site'") as $source) {

            if (!$site = node_load($source->nid)) {
                continue;
                //echo "Failed to load node id " . $source->nid . PHP_EOL;
                //exit("site2 = ". print_r($site, true));
            }
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
            //echo "client = " . print_r($node, true) . PHP_EOL;
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
            // echo "site = " . print_r($site, true) . PHP_EOL;
            node_save($site);

            // create mapping table entry to allow future redirects
            //$this->map('client', $source->nid, $node->nid);

        }

    }

    public function comments() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            'revision' => 1
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'pid', 'nid', 'uid', 'subject', 'status', 'hostname', 'name', 'mail', 'homepage'
        );

        //$max_cid                = db_query("SELECT MAX(cid) FROM comment")->fetchField();
        //$min_ticket_comment_cid = db_query("SELECT MIN(entity_id) FROM circle_import_data WHERE entity_type = 'support_ticket_comment'")->fetchField();

        db_query("TRUNCATE TABLE comment");
        db_query("TRUNCATE TABLE field_data_comment_body");
        db_query("TRUNCATE TABLE field_revision_comment_body");
        db_query("TRUNCATE TABLE ci_support_comments");
      
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
                'format' => $source->format == 1 ? 4 : $source->format // NB: input format 1 (filtered html) doesn't exist on D7 site
            );

            //$comment->is_new = !$this->comment_exists($comment->cid);

            // determine / set node type

            $node = node_load($comment->nid);

            switch ($node->type) {
                case 'support_ticket':
                    $comment->node_type = 'comment_node_support_ticket';
                    break;
                case 'todo':
                    $comment->node_type = 'comment_node_todo';
                    break;
                default:
                    drush_log('Unable to determine node type for comment id: ' . $source->cid . ' .. ' . print_r($node, true), 'warning');
            }

            drush_log('Inserting comment: ' . $comment->subject, 'ok');
                        
            comment_submit($comment);
            comment_save($comment);

            // update created / changed via db query - comment_save() will ignore these if set
            if (isset($source->timestamp) and !empty($source->timestamp))
                db_query("UPDATE comment SET changed = :timestamp, created = :timestamp WHERE cid = :cid", array(
                    ':timestamp' => $source->timestamp,
                    ':cid'       => $comment->cid
                ));
            
            switch ($node->type) {
                
                case 'support_ticket':
                case 'todo':
                    
                    if (/*$source->cid >= $min_ticket_comment_cid*/1) {

                        if (!$ticket_comment = $this->loadSupportEntity('ticket_comment', $source->cid)) {
                            drush_log('Unable to load support_ticket_comment record for cid ' . $source->cid, 'warning');
                            break;
                        }

                        db_query("
                            REPLACE INTO ci_support_comments (
                                content_nid, comment_id, comment_author, comment_title, private_comment, assigned, prev_assigned,
                                subscribed, priority, prev_priority, state, prev_state
                            ) VALUES (
                                :content_nid, :comment_id, :comment_author, :comment_title, :private_comment, :assigned, :prev_assigned,
                                :subscribed, :priority, :prev_priority, :state, :prev_state                           
                            )
                        ", array(
                              ':content_nid'     => $comment->nid,
                              ':comment_id'      => $comment->cid,
                              ':comment_author'  => $comment->uid,
                              ':comment_title'   => $comment->subject,
                              ':private_comment' => '',
                              ':assigned'        => $ticket_comment->assigned,
                              ':prev_assigned'   => $ticket_comment->assigned,
                              ':subscribed'      => '',
                              ':priority'        => $ticket_comment->priority,
                              ':prev_priority'   => $ticket_comment->priority,
                              ':state'           => $ticket_comment->state,
                              ':prev_state'      => $ticket_comment->state

                           )
                        );

                    }

                    break;
            
            }

        }

        db_query("UPDATE comment SET status = 1 WHERE 1");
        db_query("update field_data_comment_body set comment_body_format = 4 where comment_body_format = 1");
        db_query("update field_revision_comment_body set comment_body_format = 4 where comment_body_format = 1");
        
    }

    public function fixComments() {

        foreach (db_query("SELECT nid FROM node WHERE type = 'support_ticket'") as $node) {
            $nid = $node->nid;
            $comment_count = db_query("SELECT COUNT(*) FROM comment WHERE nid = $nid")->fetchField();
            $comment = db_query("SELECT * FROM comment WHERE nid = $nid ORDER BY cid DESC LIMIT 1")->fetchObject();
            if (!$comment->cid)
                $comment->cid = 0;
            echo "Updating node_comment_statistics for node id $nid\n";
            db_query("UPDATE node_comment_statistics SET comment_count = $comment_count, cid = {$comment->cid} WHERE nid = $nid");
        }

    }

    // Main import functions //

    public function notes() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            'revision' => 1
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
            if (!$source = unserialize($source->data))
                continue;
            
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
            if ($source->og_groups and is_array($source->og_groups))
                foreach ($source->og_groups as $gid)
                    $node->og_group_ref['und'][] = array('target_id' => $gid);

            // transfer any existing taxonomy terms
            if (is_array($source->taxonomy and $source->taxonomy)) {
                foreach ($source->taxonomy as $tid => $term) {
                    $destination_field = 'taxonomy_vocabulary_' . $term->vid;
                    $node->{$destination_field}['und'][] = array(
                        'tid' => $tid
                    );
                }
            }
            //print "Source = ";
            //print_r($source);
            //print "Node = ";
            //print_r($node);
            
            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' notes node: ' . $node->title, 'ok');

            // save new node
            node_save($node);

            # set 'changed' date to whatever it was on the old site (node_save() will set to current time)
            db_query("UPDATE node SET changed = :changed WHERE nid = :nid", array(
                ':changed' => $source->changed,
                ':nid'     => $node->nid
            ));

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

    public function og_assign_content() {

        foreach (db_query("SELECT data FROM circle_import_data WHERE entity_type = 'og_ancestry'") as $source) {
            $source = unserialize($source->data);
            // if node not already assigned, assign node
            if (!db_query("SELECT 1 FROM og_membership WHERE entity_type = 'node' AND etid = {$source->nid}")) {
                drush_log('Inserting og_membership record for node id ' . $source->nid . ': ' . node_load($source->nid)->title, 'ok');
                db_query("
                    INSERT INTO og_membership (id, type, etid, entity_type, gid, state, created, group_type, field_name, language
                    ) VALUES (NULL, 'og_membership_type_default', {$source->nid}, 'node', {$source->gid}, 1, CURRENT_TIMESTAMP(), 'node', 'og_group_ref', 'en')
                ");
            } else drush_log('Skipping, existing og membership record for ' . $source->nid . ': ' . node_load($source->nid)->title, 'warning');
        }

    }

    public function og_assign_users() {
        
        foreach (db_query("SELECT data FROM circle_import_data WHERE entity_type = 'og_uid'") as $source) {
            
            $source = unserialize($source->data);

            if (!db_query("SELECT 1 FROM og_membership WHERE entity_type = 'user' AND etid = {$source->uid} AND gid = {$source->nid}")->fetchField()) { 
                if ($source->is_active) {
                    drush_log('Creating og membership for user id ' . $source->uid, 'ok');
                    db_query("
                        INSERT INTO og_membership (id, type, etid, entity_type, gid, state, created, group_type, field_name, language
                        ) VALUES (NULL, 'og_membership_type_default', {$source->uid}, 'user', {$source->nid}, 1, {$source->created}, 'node', 'og_group_ref', 'en')
                    ");

                }

            }
            // insert / update og roles - there are only two, administrator and member
            // so look at is_admin field to determine the role
            if ($source->is_admin) {
                // remove role 8 (member) if they have it, even though it's 
                // probably ok to keep
                db_query("DELETE FROM og_users_roles WHERE uid = {$source->uid} AND gid = {$source->nid} AND rid IN (7, 8) AND group_type = 'node'");
                // assign rid 9 (administrator member)
                db_query("REPLACE INTO og_users_roles (uid, rid, gid, group_type) VALUES ({$source->uid}, 9, {$source->nid}, 'node')");
            } else {
                // remove role 9 (administrator member) and 7 (non-member) if they have it
                db_query("DELETE FROM og_users_roles WHERE uid = {$source->uid} AND gid = {$source->nid} AND rid IN (7, 9) AND group_type = 'node'");
                // assign rid 8 (member)
                db_query("REPLACE INTO og_users_roles (uid, rid, gid, group_type) VALUES ({$source->uid}, 8, {$source->nid}, 'node')");
            }

        }       
    
    }

    /**
     * Perform preparation tasks prior to starting import
     */
    public function prepare() {

        # delete all nodes above and including starting node
        # todo: may wish to archive a selection of nodes (eg: kb items)
        # and re-import at end
        $results = db_query("
            SELECT nid FROM node WHERE nid >= :nid
        ", array(
              ':nid' => IMPORT_INTRANET_START_NODE
           )
        );
        foreach ($results as $result) {
            drush_log("Deleting nid {$result->nid}: " . node_load($result->nid)->title, 'ok');
            node_delete($result->nid);
        }

        # delete orphaned data from field_hours_support_total
        $results = db_query("
            SELECT entity_id
            FROM field_data_field_hours_total h
            LEFT JOIN node ON node.nid = h.entity_id
            WHERE entity_type = 'node' AND node.nid IS NULL
        ");

        foreach ($results as $result) {
            drush_log("Deleting orphaned field_data_field_hours_total data for nid {$result->entity_id}", 'ok');
            db_query("
                DELETE FROM field_data_field_hours_total 
                 WHERE entity_type = 'node'
                   AND entity_id = :entity_id
            ", array(
                  ':entity_id' => $result->entity_id
               )
            );
        }

    }

    // Import 'site' content type
    public function sites() {
        
        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            'revision' => 1
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
            //print_r($node);
            node_save($node);

            # set 'changed' date to whatever it was on the old site (node_save() will set to current time)
            db_query("UPDATE node SET changed = :changed WHERE nid = :nid", array(
                ':changed' => $source->changed,
                ':nid'     => $node->nid
            ));

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
            'status'   => 1,
            'revision' => 1
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'nid', 'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:
        $properties_changed = array('field_hours_total');

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

            # update field_hours_spent - want to try and improve this so that Drupal knows
            # which hours go with which comment, but just get them onto the node for now
            if (isset($source->field_hours_spent_support) and is_array($source->field_hours_spent_support)) {
                foreach ($source->field_hours_spent_support as $hours) {
                    $node->field_hours_spent['und'][] = array(
                        'value' => $hours['value']
                    );
                }
            } else {
                drush_log('Invalid / missing data for field_hours_spent_support', 'warning');
            }
            
            # populate og group ref
            if (is_array($source->og_groups) and $source->og_groups)
                $node->og_group_ref['und'][0]['target_id'] = reset($source->og_groups);
            
            $node->is_new = !$this->node_exists($source->nid);

            // todo: taxonomy, additional fields ..

            // create mapping table entry to allow future redirects
            //$this->map('support_ticket', $source->path, $source->nid, $node->nid);

            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' support ticket node: ' . $node->title, 'ok');
            #if ($node->nid == 3822)
            #    print_r($node);

            //print_r($node);
            try {
                node_save($node);
            } catch (Exception $e) {
                drush_log('Error saving support ticket: ' . $e->getMessage(), 'error');
                continue;
            }

            if ($source->path) {
                // create path for the new node which is the same as the old one
                $path_info = array(
                    'source'   => "node/{$node->nid}",
                    'alias'    => $source->path,
                    'language' => 'und'               
                );
                path_save($path_info);
            } else {
                drush_log('No path found when importing node ' . $source->entity_id, 'warning');   
            }

            # set 'changed' date to whatever it was on the old site (node_save() will set to current time)
            db_query("UPDATE node SET changed = :changed WHERE nid = :nid", array(
                ':changed' => $source->changed,
                ':nid'     => $node->nid
            ));

        }


    }

    public function todos() {

        // some properties are the same in all cases - add those to this array:
        $properties_static = array(
            'language' => 'und',
            'revision' => 1
        );

        // some properties can be copied as is - add those this array:
        $properties_unchanged = array(
            'nid', 'uid', 'title', 'type', 'log', 'status', 'created', 'changed', 'comment',
            'promote', 'sticky', 'tnid', 'translate', 'revision_timestamp', 'revision_uid',
            'data', 'last_comment_timestamp', 'last_comment_name', 'name', 'picture'
        );

        $properties_changed = array(
            'field_assigned', 'field_completed', 'field_estimate', 'field_milestone',
            'field_completion'
        );

        // some fields need to be upgraded to D7 array syntax (with language keys)
        // add those to this array:
        $fields_mapped = array(
            'field_stage' => array(
                'Needs info'         => 0,
                'Active'             => 1,
                'Awaiting feedback'  => 2,
                'Project Management' => 3,
                'Ongoing Support'    => 4
            )
        );

        foreach (db_query("SELECT * FROM circle_import_data WHERE type = 'todo'") as $source) {
            
            // unserialize existing D6 node data
            if (!$source = unserialize($source->data)) {
                drush_log('Unable to unserialize node data', 'warning');
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

            if ($source->field_hours_spent)
                $node->field_hours_spent_todo['und'] = $source->field_hours_spent;

            // add up field_hours_spent - add to field_hours_total
            $total = 0;
            foreach ($source->field_hours_spent as $hours_spent)
                $total += $hours_spent['value'];

            $node->field_hours_total['und'][0]['value'] = number_format($total, 2);
            $node->field_stage['und'][0]['value']       = $this->translate('field_stage', $source->field_stage[0]['value']);
            
            $node->field_invoiced['und'][0]['value']           = $source->field_invoiced[0]['value'] == 'invoiced' ? 1 : 0;
            $node->field_invoice_as_project['und'][0]['value'] = $source->field_part_of_project[0]['value'] == 'invoice as part of project' ? 1 : 0;

            $node->field_monthly_support_hours['und'][0]['value'] = 
                $source->field_todo_support_hours[0]['value'] == 'Off' ? 0 : 1;

            /*
            foreach ($fields_mapped as $property => $map)
                if (isset($source->$property) and isset($map[$property]))
                    $node->$property = array('und' => $map[$source->$property[0]['value']]);
                else drush_log('Unable to locate map for ' . $property, 'warning');
            */

            $node->is_new = !$this->node_exists($node->nid);

            if (trim($source->body)) {
                $node->body['und'][0]['value']  = $source->body;
                $node->body['und'][0]['format'] = $source->format;
            }

            # populate og group ref
            if (is_array($source->og_groups) and $source->og_groups)
                $node->og_group_ref['und'][0]['target_id'] = reset($source->og_groups);

            // todo: additional fields, taxonomy etc

            // create mapping table entry to allow future redirects
            //$this->map('todo', $source->path, $source->nid, $node->nid);

            drush_log(($node->is_new ? 'Inserting' : 'Updating') . ' todo node: ' . $node->title, 'ok');
            node_save($node);

            if ($source->path) {
                // create path for the new node which is the same as the old one
                $path_info = array(
                    'source'   => "node/{$node->nid}",
                    'alias'    => $source->path,
                    'language' => 'und'               
                );
                path_save($path_info);
            } else {
                drush_log('No path found when importing node ' . $source->entity_id, 'warning');   
            }

            # set 'changed' date to whatever it was on the old site (node_save() will set to current time)
            db_query("UPDATE node SET changed = :changed WHERE nid = :nid", array(
                ':changed' => $source->changed,
                ':nid'     => $node->nid
            ));

        }

    }

    /**
     * Import users from csv file
     */
    public function users() {

        $user_exists = function($uid) {
            return (bool)db_query("SELECT COUNT(*) FROM users WHERE uid = :uid", array(
                ':uid' => $uid
            ))->fetchField();
        };

        # delete all test user accounts - do this through the api, which should unassign any created content
        /*
        foreach (db_query("SELECT uid FROM users WHERE uid >= 562") as $user) {
            drush_log('Deleting user ' . $user->uid, 'ok');
            user_delete($user->uid);
        }
        */

        # prepare to do user import
        require_once DRUPAL_ROOT . "/includes/password.inc";
        $counter = 0;

        # this is deliberately done with queries to avoid firing hooks .. we just want to 
        # quietly upgrade all user accounts / import new users + retain same uids as D6
        db_query("DELETE FROM users WHERE uid > 1");
        db_query("DELETE FROM users_roles WHERE uid > 1");

        foreach (db_query("SELECT * FROM circle_import_data WHERE entity_type = 'user'") as $d6_account) {
            
            $row = (array)unserialize($d6_account->data);
            if ($row['uid'] <= 1)
                continue;

            //convert password from Drupal 6 style to Drupal 7 style
            $hashed_pass='U'.user_hash_password($row['pass'],11);
            //print_r($row);
            
            db_query("
                REPLACE INTO users (
                    uid, name, pass, mail, theme, signature, created, access, status, 
                    timezone, language, init, data, login, signature_format
                ) VALUES (
                    :uid, :name, :pass, :mail, :theme, :signature, :created, :access, :status, 
                    :timezone, :language, :init, :data, :login, :signature_format
                )
            ", array(
                  ':uid'       => $row['uid'],
                  ':name'      => $row['name'],
                  ':pass'      => $hashed_pass,
                  ':mail'      => $row['mail'],
                  ':theme'     => $row['theme'],
                  ':signature' => $row['signature'],
                  ':created'   => $row['created'],
                  ':access'    => $row['access'],
                  ':status'    => $row['status'],
                  ':timezone'  => $row['timezone'],
                  ':language'  => $row['language'],
                  ':init'      => $row['init'],
                  ':data'      => $row['data'],
                  ':login'     => $row['login'],
                  ':signature_format' => 4
               )
            );
            drush_log('Recreated user account for ' . $row['name'], 'ok');

            foreach ($row['roles'] as $rid => $role_name) {
                db_query("REPLACE INTO users_roles (uid, rid) VALUES (:uid, :rid)", array(
                    ':uid' => $row['uid'],
                    ':rid' => $rid
                ));
                drush_log("Assigning role '$role_name' to {$row['name']}", 'ok');
            }

            if($row['status']==1){



                //check if user with same email address already exists in Drupal 7 database, if it does, do not migrate
                /*
                if (!$loaded = user_load($row['uid'])) {
                    
                    $account = new StdClass;

                    $account->uid      = $row['uid'];
                    $account->is_new   = TRUE;
                    $account->name     = $row['name'];
                    $account->pass     = $hashed_pass;
                    $account->mail     = $row['mail'];
                    $account->init     = $row['mail'];
                    $account->status   = TRUE;
                    $account->roles    = $row['roles'];
                    $account->timezone = variable_get('date_default_timezone', '');
                    //create user in Drupal 7 site 
                    user_save($account);
                    //print_r($account);
                    //print message
                    echo "User acount ".$row['name']." has been " . ($account->is_new ? 'created' : 'updated') . "\n";
                    $counter++;
                } else {
                    echo $row['name'] . ' (' . $row['mail'] . ' - uid: ' . $row['uid'] . ' - account already exists for uid ' . $loaded->uid . " - not imported\n";
                }
                */

            }
        
        }      

        /*
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
        */

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

    private function createSupportContentEntity($content_nid) {
        if ($group_nid = $this->getContentGroup($content_nid)) {
            if ($client_nid = $this->getClientForSite($group_nid)) {
                
                $assigned   = $this->getSupportAssigned($content_nid);
                $subscribed = $this->getSupportSubscribed($content_nid);
                $ticket     = $this->loadSupportEntity('ticket', $content_nid);
                $priority   = $ticket->priority;
                $status     = $this->getContentState($content_nid);
                $complete   = $status == 4 ? 1 : 0;

                echo "State = " . $status . PHP_EOL;

                drush_log('Creating ci_support_content entity for nid ' . $content_nid, 'ok');

                db_query("
                    REPLACE INTO ci_support_content (content_nid, client_nid, assigned, subscribed, status, priority, complete)
                          VALUES (:content_nid, :client_nid, :assigned, :subscribed, :status, :priority, :complete)
                ", array(
                      ':content_nid' => $content_nid,
                      ':client_nid'  => $group_nid,
                      ':assigned'    => $assigned ? $assigned : 350, // assign to maya if no one assigned
                      ':subscribed'  => serialize($subscribed),
                      ':status'      => $status,
                      ':priority'    => $priority,
                      ':complete'    => $complete
                   )
                );
            } else {
                drush_log('Unable to get client for site nid ' . $group_nid . ': ' . node_load($group_nid)->title, 'warning');
            }
        } else {
            drush_log('Unable to get OG Membership for nid ' . $content_nid . ': ' . node_load($content_nid)->title, 'warning');
        }
    }

    private function getClientForSite($site_nid) {
        return db_query("
            SELECT field_client_nid FROM field_data_field_client
             WHERE entity_type = 'node' AND entity_id = :entity_id 
        ", array(
              ':entity_id' => $site_nid
           )
        )->fetchField();
    }

    private function getContentGroup($content_nid) {
        return db_query("
            SELECT parent_entity_id FROM circle_import_data 
             WHERE entity_type = 'og_ancestry'
               AND entity_id = :content_nid
        ", array(
              ':content_nid' => $content_nid
           )
        )->fetchField();
    }

    private function getContentState($nid) {
        return db_query("SELECT state FROM support_ticket WHERE nid = " . $nid)->fetchField();
    }

    private function getSupportAssigned($ticket_nid) {
        if ($entity = $this->loadSupportEntity('ticket', $ticket_nid))
            return $entity->assigned;
    }

    private function getSupportSubscribed($ticket_nid) {
        $results = db_query("
            SELECT uid FROM support_assigned WHERE nid = :ticket_nid AND active
        ", array(
              ':ticket_nid' => $ticket_nid
           )
        );
        $subscribed = array();
        foreach ($results as $result)
            $subscribed[] = $result->uid;

        return $subscribed;
        
    }

    private function hasSupportContentEntity($content_nid) {
        return (bool)db_query("
            SELECT client_nid FROM ci_support_content WHERE content_nid = :content_nid
        ", array(
              ':content_nid' => $content_nid
           )
        )->fetchField();
    }

    /**
     * Helper function to load support metadata, such as ticket priority, assigned etc
     * @param  $entity_name - the name of the entity (minus 'support_')
     * @param  $entity_id   - the entity_id to retrieve
     * @return StdClass     - the unserialized entity
     */
    private function loadSupportEntity($entity_name, $entity_id) {
        
        static $pk_names = array(
            'support_ticket'         => 'nid', 
            'support_ticket_comment' => 'cid', 
            'support_assigned'       => 'nid', 
            'support_client'         => 'clid',
            'support_og_mapping'     => 'gid', 
            'support_priority'       => 'pid', 
            'support_states'         => 'sid'
        );

        $entity_name = 'support_' . $entity_name; 
        
        if (!isset($pk_names[$entity_name]))
            return drush_log(dt(
                "Non-existent entity name '$entity_name' in @class::@method",
                array(
                    '@class'  => __CLASS__,
                    '@method' => __METHOD__
                )
            ), 'error');

        $pk_name = $pk_names[$entity_name];
    
        return db_query("
            SELECT * FROM $entity_name WHERE $pk_name = :entity_id
        ", array(
              ':entity_id' => $entity_id
           )
        )->fetchObject();
        

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

    /**
     * Translate field keys
     */
    private function translate($field_name, $field_value) {
        $mapping = array(
            'field_stage' => array(
                'Needs info'         => 0,
                'Active'             => 1,
                'Awaiting feedback'  => 2,
                'Project Management' => 3,
                'Ongoing Support'    => 4
            )
        );
        if (isset($mapping[$field_name]))
            return $mapping[$field_name][$field_value];
    } 


};

/**
 * Main command callback
 */
function drush_import_intranet() {
    
    $start_time      = microtime(true);
    $disable_modules = array('pathauto', 'ci_support', 'tracker');
    $import          = new IntranetImport();
/*
    drush_print('Disabling modules: ' . implode(', ', $disable_modules) . '...');
    module_disable($disable_modules);
    drush_log('Success!', 'ok');
    
    $import->prepare();
    $import->users();

    // do ogs first, because other things link to them
    $import->sites();
    
    # import other content items
    $import->notes();
    $import->support_tickets();
    $import->todos();

    # clients should be done at the end as these are new content items - we don't
    # want to accidentally use node ids that will be needed for imported content
    $import->clients();

    # assign todos and support tickets to clients, now they've been created
 */   
    $import->assignSupportContent();
    $import->assignTodoContent();
  /*  
    # import comments and link to content
    $import->comments();
    
    $import->og_assign_content();    
    $import->og_assign_users();

    $import->fixComments();

    # todo 
    //$import->files();
    
    drush_print('Enabling modules: ' . implode(', ', $disable_modules) . '...');
    module_enable($disable_modules);
    drush_log('Success!', 'ok');
*/
    drush_log(sprintf("Import completed in %.2f seconds\n\n", microtime(true) - $start_time), 'ok');


}

