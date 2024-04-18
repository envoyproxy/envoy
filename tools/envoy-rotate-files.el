;;; Defines a function to rotate between related Envoy source
;;; files. This requires that ./find_related_envoy_files.py be placed
;;; on $PATH and a key-binding in .emacs to elisp command
;;; envoy-rotate-files. E.g.
;;;
;;; (load-file "GIT_CLIENT/envoy/tools/envoy-rotate-files.el")
;;; (defun envoy-setup-hooks ()
;;;   "Sets up Emacs customizations for editing in the Envoy codebase"
;;;   (local-set-key (quote [C-tab]) 'envoy-rotate-files))
;;;
;;; (add-hook 'c-mode-common-hook 'envoy-setup-hooks t)

(defun envoy-rotate-files ()
  "Rotate the current buffer based on envoy cc file patterns"
  (interactive)
  (let ((related-files
         (split-string
          (shell-command-to-string
           (concat "find_related_envoy_files.py " (buffer-file-name))))))
    (if (null related-files)
        (error (concat "File " (buffer-file-name)
                       " does not appear to be an envoy C++ source file"))
      (find-file (car related-files)))))
