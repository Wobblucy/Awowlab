<?php
// export_batch.php - Batch export all DB2 files from a folder
//
// Changed from example.php in erorus/db2 (Apache License 2.0, see ../LICENSE):
// it walks a folder of .db2 files and writes every record of each to a text
// or CSV file instead of printing a sample.

if ($argc < 3) {
    echo "Usage: php export_batch.php <source_folder> <target_folder> [options]\n";
    echo "\nOptions:\n";
    echo "  --csv          Export as CSV instead of text\n";
    echo "  --keep-name    Keep original filename without _export suffix\n";
    echo "  --recursive    Process subdirectories\n";
    echo "\nExamples:\n";
    echo "  php export_batch.php \"C:\\db2files\" \"C:\\output\"\n";
    echo "  php export_batch.php \"C:\\db2files\" \"C:\\output\" --csv\n";
    echo "  php export_batch.php \"C:\\db2files\" \"C:\\output\" --keep-name --recursive\n";
    exit(1);
}

require_once __DIR__ . '/src/autoload.php';
use \Erorus\DB2\Reader;

$sourceFolder = rtrim($argv[1], '/\\');
$targetFolder = rtrim($argv[2], '/\\');
$exportAsCsv = in_array('--csv', $argv);
$keepName = in_array('--keep-name', $argv);
$recursive = in_array('--recursive', $argv);

// Create target folder if needed
if (!is_dir($targetFolder)) {
    if (!mkdir($targetFolder, 0777, true)) {
        echo "Error: Could not create target folder: $targetFolder\n";
        exit(1);
    }
}

// Validate source folder
if (!is_dir($sourceFolder)) {
    echo "Error: Source folder does not exist: $sourceFolder\n";
    exit(1);
}

$total = 0;
$success = 0;
$failed = 0;

echo "Starting DB2 export process...\n";
echo "Source: $sourceFolder\n";
echo "Target: $targetFolder\n";
echo "Format: " . ($exportAsCsv ? "CSV" : "TXT") . "\n";
echo "Recursive: " . ($recursive ? "Yes" : "No") . "\n";
echo "================================\n";

// Function to get all db2 files
function getDb2Files($dir, $recursive) {
    $files = [];

    if ($recursive) {
        $iterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator($dir, RecursiveDirectoryIterator::SKIP_DOTS),
            RecursiveIteratorIterator::SELF_FIRST
        );

        foreach ($iterator as $file) {
            if ($file->isFile() && strtolower($file->getExtension()) === 'db2') {
                $files[] = $file->getPathname();
            }
        }
    } else {
        $pattern = $dir . DIRECTORY_SEPARATOR . '*.db2';
        $files = glob($pattern);
    }

    return $files;
}

$db2Files = getDb2Files($sourceFolder, $recursive);

if (empty($db2Files)) {
    echo "No .db2 files found in source folder.\n";
    exit(0);
}

echo "Found " . count($db2Files) . " DB2 file(s)\n\n";

foreach ($db2Files as $db2File) {
    $filename = basename($db2File);

    if ($keepName) {
        $outputName = str_replace('.db2', ($exportAsCsv ? '.csv' : '.txt'), $filename);
    } else {
        $outputName = str_replace('.db2', '_export' . ($exportAsCsv ? '.csv' : '.txt'), $filename);
    }

    $outputPath = $targetFolder . DIRECTORY_SEPARATOR . $outputName;

    echo "Processing: $filename\n";
    echo "Output: $outputName\n";

    try {
        $reader = new Reader($db2File);
        ob_start();

        echo "Layout: ", dechex($reader->getLayoutHash()), "\n";

        $recordCount = 0;
        foreach ($reader->generateRecords() as $id => $record) {
            if ($exportAsCsv) {
                // CSV format with proper escaping
                $row = [$id];
                foreach ($record as $colName => $colVal) {
                    if (is_array($colVal)) {
                        $row[] = '"[' . implode(',', $colVal) . ']"';
                    } else {
                        $row[] = $colVal;
                    }
                }
                echo implode(',', $row) . "\n";
            } else {
                // Text format (same as example.php)
                echo $id . ": ";
                $colNum = 0;
                foreach ($record as $colName => $colVal) {
                    if ($colNum++ > 0) echo ",";
                    if (is_array($colVal)) {
                        echo '[', implode(',', $colVal), ']';
                    } else {
                        echo $colVal;
                    }
                }
                echo "\n";
            }
            $recordCount++;
        }

        $content = ob_get_clean();
        file_put_contents($outputPath, $content);

        $fileSize = filesize($outputPath);
        $fileSizeKB = number_format($fileSize / 1024, 2);

        echo "[SUCCESS] Size: {$fileSizeKB} KB, Records: $recordCount\n";
        $success++;

    } catch (Exception $e) {
        ob_end_clean();
        echo "[FAILED] Error: " . $e->getMessage() . "\n";
        $failed++;
    }

    $total++;
    echo "\n";
}

echo "================================\n";
echo "Export complete!\n";
echo "Total files processed: $total\n";
echo "Successful: $success\n";
echo "Failed: $failed\n";
echo "================================\n";
