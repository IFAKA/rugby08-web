// Page errors reach the console with their stack (the shell only shows "Exception thrown").
(function () {
  if (typeof window === 'undefined') return; // this file also runs in the pthread workers
  window.addEventListener('error', function (e) {
    console.warn('JS error:', e.message, e.filename + ':' + e.lineno, e.error && e.error.stack);
  });
  window.addEventListener('unhandledrejection', function (e) {
    console.warn('unhandled rejection:', e.reason && (e.reason.stack || e.reason));
  });
})();
