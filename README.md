intranet-migrate
================

Custom D6 -> D7 import for Circle Intranet.

Step 1) consists of an export script to run on a D6, it runs node_load or equivalent entity load function against all content we want to export, serializes and stores to an import_data table.

Step 2) transfer the import_data table to destination D7 db.

Step 3) consists of an import script to run against the destination D7. This will pull out all the content we exported, deserialize, construct D7 style objects and save them. Should retain original entity ids / path aliases to avoid need for redirection.