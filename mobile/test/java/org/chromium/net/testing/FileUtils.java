package org.chromium.net.testing;

import android.util.Log;
import java.io.File;
import java.util.function.Function;

/**
 * Helper methods for dealing with Files.
 */
public final class FileUtils {
  private static final String TAG = "FileUtils";

  public static Function<String, Boolean> DELETE_ALL = filepath -> true;

  /**
   * Delete the given File and (if it's a directory) everything within it.
   * @param currentFile The file or directory to delete. Does not need to exist.
   * @param canDelete the {@link Function} function used to check if the file can be deleted.
   * @return True if the files are deleted, or files reserved by |canDelete|, false if failed to
   *         delete files.
   * @note Caveat: Return values from recursive deletes are ignored.
   * @note Caveat: |canDelete| is not robust; see https://crbug.com/1066733.
   */
  public static boolean recursivelyDeleteFile(File currentFile,
                                              Function<String, Boolean> canDelete) {
    if (!currentFile.exists()) {
      // This file could be a broken symlink, so try to delete. If we don't delete a broken
      // symlink, the directory containing it cannot be deleted.
      currentFile.delete();
      return true;
    }
    if (canDelete != null && !canDelete.apply(currentFile.getPath())) {
      return true;
    }

    if (currentFile.isDirectory()) {
      File[] files = currentFile.listFiles();
      if (files != null) {
        for (File file : files) {
          recursivelyDeleteFile(file, canDelete);
        }
      }
    }

    boolean ret = currentFile.delete();
    if (!ret) {
      Log.e(TAG, "Failed to delete: " + currentFile);
    }
    return ret;
  }

  private FileUtils() {}
}
