;; target argument, populated with actual project targets
;; config argument, populated with actual project configs

(require 'transient)
(require 'comint)
(require 'project)

(define-derived-mode mesche-mode scheme-mode "Mesche"
  "Major mode for editing Mesche code."
  :group 'lisp

  ;; Add custom keywords
  ;; (setq font-lock-defaults (append '()
  ;;                                  font-lock-defaults))

  ;; Customize Scheme indentation
  (put 'suite 'scheme-indent-function 'defun)
  (put 'verify 'scheme-indent-function 'defun)
  (put 'test 'scheme-indent-function 'defun))

(defvar mesche-cli-bin-path "/home/daviwil/Projects/Code/ggo2022-call-of-the-stones/deps/mesche-lang/mesche/tools/mesche/mesche")

(defun mesche--read-project ()
  ;; TODO: Walk up the directory hierarchy until you find a .git or project.msc
  (read (shell-command-to-string (concat mesche-cli-bin-path " project"))))

(defun mesche--get-project-targets (complete-me filter-p completion-type)
  (cdar (mesche--read-project)))

(defun mesche--get-project-configs ()
  (cdadr (mesche--read-project)))

;; (transient-define-argument mesche-build-command--target-argument ()
;;   :description "Build Target"
;;   :class transient-switches
;;   :key "t"
;;   :argument-format "%s"
;;   :argument-regexp "\\.*"
;;   :choices mesche--get-project-targets)

(transient-define-argument mesche-build-command--config-argument ()
  :description "Configuration"
  :class transient-switches
  :key "c"
  :argument-format "--config %s"
  :argument-regexp "\\.*"
  :choices '("debug" "release"))

(defun mesche-build-command--execute ()
  (interactive)
  (message "%s" (transient-args transient-current-command)))

(transient-define-suffix mesche-build-command--build-project (&optional args)
  "Show current infix args"
  :key "b"
  :description "Build Project"
  (interactive (list (transient-args transient-current-command)))
  (message (concat "mesche build " (string-join args " "))))

(transient-define-suffix mesche-build-command--run-project (&optional args)
  "Show current infix args"
  :key "r"
  :description "Run Project"
  (interactive (list (transient-args transient-current-command)))
  (message (concat "mesche run " (string-join args " "))))

(transient-define-prefix mesche-build-command ()
  "Build command arguments"
  [["Arguments"
    ;; (mesche-build-command--target-argument)
    ;; ("t" "Build Target" "--target=" :choices mesche--get-project-targets)
    (mesche-build-command--config-argument)]
   ["Actions"
    (mesche-build-command--build-project)
    (mesche-build-command--run-project)]])

(transient-define-prefix mesche-run-command ()
  "Mesche CLI Interface"
  [["Current Project"
    ("b" "build" mesche-build-command)
    ("d" "deps" (lambda () (interactive) (message "hello")))
    ("r" "repl" (lambda () (interactive) (message "hello")))
    ("i" "install" (lambda () (interactive) (message "hello")))]

   ["Global Commands"
    ("n" "new" (lambda () (interactive) (message "hello")))
    ("v" "version" (lambda () (interactive) (message "hello")))]])

(defvar mesche-repl-buffer-name "*mesche: repl*")

(defun run-mesche ()
  "Run the Mesche REPL inside an interactive buffer."
  (interactive)
  (let* ((default-directory (if (project-current)
                                (project-root (project-current))
                              default-directory))
         (buffer (get-buffer-create mesche-repl-buffer-name))
         (proc-alive (comint-check-proc buffer))
         (process (get-buffer-process buffer)))
    ;; if the process is dead then re-create the process and reset the
    ;; mode.
    (unless proc-alive
      (with-current-buffer buffer
        (apply 'make-comint-in-buffer "Mesche" buffer
               mesche-cli-bin-path nil '("repl"))
        ;; Use mesche-repl-mode eventually
        (comint-mode)))
    ;; Regardless, provided we have a valid buffer, we pop to it.
    (when buffer
      (pop-to-buffer buffer))))

(defconst mesche--module-regexp "^(define-module \\((.*)\\)")

(defun mesche--get-module-current-buffer ()
  "Find current module, or normalize MODULE."
  (save-excursion
    (beginning-of-buffer)
    (if (re-search-forward geiser-mesche--module-regexp nil t)
        (car (geiser-syntax--read-from-string (match-string-no-properties 1)))
      nil)))

;; Eval a region or top-level expression in the context of a module
(defun mesche-eval-region ()
  (message "EVAL REGION: %s" (buffer-substring-no-properties
                              (region-beginning)
                              (region-end))))

(defun mesche-eval-defun ()
  (interactive)
  (save-mark-and-excursion
    (let* ((module (mesche--get-module-current-buffer))
           (defun-string (progn
                           (mark-defun)
                           (format "(begin\n(module-enter %S)\n%s)"
                                   (if module module '(mesche-user))
                                   (buffer-substring-no-properties
                                    (region-beginning)
                                    (region-end)))))
           (repl-buffer (get-buffer mesche-repl-buffer-name)))
        (if repl-buffer
            (progn
              ;; Switch to the repl buffer and delete existing input
              (switch-to-buffer repl-buffer)
              (comint-delete-input)
              (comint-bol)
              (insert (string-replace "\n" " " defun-string))
              (comint-send-input))))))

(define-key mesche-mode-map (kbd "C-M-x") #'mesche-eval-defun)

(provide 'mesche)
