// Basic interactivity: contact form AJAX + simple validation
document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('contactForm');
  if (form) {
    form.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const data = new FormData(form);
      const params = new URLSearchParams();
      for (const pair of data.entries()) params.append(pair[0], pair[1]);

      const alertBox = document.getElementById('formAlert');
      alertBox.textContent = 'Sending...';

      try {
        const resp = await fetch('/contact', {
          method: 'POST',
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: params.toString()
        });
        if (!resp.ok) throw new Error('Network response not ok');
        const json = await resp.json();
        if (json.ok) {
          alertBox.className = 'text-success';
          alertBox.textContent = 'Message sent — we will contact you soon.';
          form.reset();
        } else {
          alertBox.className = 'text-danger';
          alertBox.textContent = 'Error: ' + (json.error || 'unknown');
        }
      } catch (err) {
        alertBox.className = 'text-danger';
        alertBox.textContent = 'Could not send message. (Are you running the server?)';
        console.error(err);
      }
    });
  }
});
